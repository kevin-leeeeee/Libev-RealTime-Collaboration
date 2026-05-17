#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ev.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

#define MAX_BUFFER 65536 // 注意：真實網卡 MTU 通常為 1500，超過需實作分片 (Fragmentation)。在 loopback (lo) 介面上測試可支援大封包。
#define NICKNAME_LEN 32
#define CUSTOM_ETH_TYPE 0x88B5

// Custom Protocol Message Types
#define MSG_JOIN 0
#define MSG_DATA 1
#define MSG_LEAVE 2
#define MSG_PING 3
#define MSG_PONG 4

// ANSI Colors for Terminal
#define COLOR_RESET "\033[0m"
#define COLOR_SYS   "\033[1;33m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_USER  "\033[1;36m"
#define COLOR_GAME  "\033[1;32m"

int raw_socket_fd;
int ifindex;
unsigned char server_mac[6];

typedef struct client_node {
    unsigned char mac[6];
    char nickname[NICKNAME_LEN];
    int has_joined;
    int ping_missed;
    struct client_node *prev;
    struct client_node *next;
} client_t;

client_t *head = NULL;
struct ev_loop *main_loop = NULL;
ev_timer user_list_timer;
ev_timer heartbeat_timer;


int game_active = 0;
int game_target = 0;
int game_min = 1;
int game_max = 100;

// Base64 解碼表
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_decode(const char *input, char *output) {
    int i, j, k;
    int table[256];
    for (i = 0; i < 256; i++) table[i] = -1;
    for (i = 0; i < 64; i++) table[(int)base64_table[i]] = i;

    int len = strlen(input);
    for (i = 0, j = 0; i < len; i += 4) {
        int v = 0;
        int count = 0;
        for (k = 0; k < 4 && i + k < len; k++) {
            if (input[i + k] == '=') break;
            v = (v << 6) | table[(int)input[i + k]];
            count++;
        }
        for (int l = 0; l < count - 1; l++) {
            output[j++] = (v >> (8 * (count - 2 - l))) & 0xFF;
        }
    }
    output[j] = '\0';
    return j;
}

int base64_encode(const unsigned char *input, int len, char *output) {
    int i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        int v = input[i] << 16;
        if (i + 1 < len) v |= input[i + 1] << 8;
        if (i + 2 < len) v |= input[i + 2];

        output[j++] = base64_table[(v >> 18) & 0x3F];
        output[j++] = base64_table[(v >> 12) & 0x3F];
        output[j++] = (i + 1 < len) ? base64_table[(v >> 6) & 0x3F] : '=';
        output[j++] = (i + 2 < len) ? base64_table[v & 0x3F] : '=';
    }
    output[j] = '\0';
    return j;
}

// 儲存多個文件的狀態
typedef struct {
    char name[64];
    char content[MAX_BUFFER];
} file_state_t;

#define MAX_FILES 20
file_state_t doc_files[MAX_FILES];
int file_count = 0;

void broadcast_file_list();
void broadcast_user_list();
void broadcast_message(client_t *sender, const char *msg, int is_system, int is_game, int is_raw);
void send_to_client(client_t *client, const char *msg, int is_system, int is_game);
void send_eth_frame(const unsigned char *dest_mac, uint8_t msg_type, const char *payload, int payload_len);
void remove_client(client_t *client);

void user_list_timer_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
    broadcast_user_list();
}

void heartbeat_timer_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
    client_t *curr = head;
    client_t *tmp;
    while (curr) {
        tmp = curr->next;
        curr->ping_missed++;
        if (curr->ping_missed > 3) {
            printf(COLOR_SYS "[TIMEOUT] Client '%s' (%02X:%02X) timed out (no pong).\n" COLOR_RESET,
                   curr->nickname, curr->mac[4], curr->mac[5]);
            remove_client(curr);
        } else {
            send_eth_frame(curr->mac, MSG_PING, "", 0);
        }
        curr = tmp;
    }
}

void trigger_user_list_broadcast() {
    ev_timer_stop(main_loop, &user_list_timer);
    ev_timer_set(&user_list_timer, 0.2, 0.);
    ev_timer_start(main_loop, &user_list_timer);
}

// 透過 Raw Socket 發送自訂 Ethernet Frame
void send_eth_frame(const unsigned char *dest_mac, uint8_t msg_type, const char *payload, int payload_len) {
    if (payload_len > MAX_BUFFER) payload_len = MAX_BUFFER; // 避免溢位
    
    int frame_len = 17 + payload_len;
    char *buffer = malloc(frame_len);
    if (!buffer) return;
    
    struct ethhdr *eh = (struct ethhdr *)buffer;
    memcpy(eh->h_dest, dest_mac, 6);
    memcpy(eh->h_source, server_mac, 6);
    eh->h_proto = htons(CUSTOM_ETH_TYPE);
    
    buffer[14] = msg_type;
    uint16_t len_net = htons((uint16_t)payload_len);
    memcpy(buffer + 15, &len_net, 2);
    
    if (payload_len > 0) {
        memcpy(buffer + 17, payload, payload_len);
    }
    
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sll_ifindex = ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dest_mac, 6);
    
    int bytes_sent = sendto(raw_socket_fd, buffer, frame_len, 0, (struct sockaddr*)&socket_address, sizeof(socket_address));
    if (bytes_sent < 0) {
        perror("sendto failed (possibly MTU limit exceeded on physical interface)");
    }
    free(buffer);
}

void save_file_to_disk(int index, const char *base64_content) {
    if (index < 0 || index >= file_count) return;
    strncpy(doc_files[index].content, base64_content, MAX_BUFFER - 1);
    doc_files[index].content[MAX_BUFFER - 1] = '\0';

    char *decoded = malloc(MAX_BUFFER);
    if (!decoded) return;
    base64_decode(base64_content, decoded);
    
    FILE *fp = fopen(doc_files[index].name, "w");
    if (fp) {
        fputs(decoded, fp);
        fclose(fp);
        printf(COLOR_SYS "[DISK] Saved %s\n" COLOR_RESET, doc_files[index].name);
    }
    free(decoded);
}

void update_files_index() {
    FILE *idx = fopen("files_index.txt", "w");
    if (!idx) return;
    for (int i = 0; i < file_count; i++) {
        fprintf(idx, "%s\n", doc_files[i].name);
    }
    fclose(idx);
}

void init_files() {
    FILE *idx = fopen("files_index.txt", "r");
    if (!idx) return;
    
    char line[128];
    while (fgets(line, sizeof(line), idx)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;
        if (file_count >= MAX_FILES) break;
        
        strncpy(doc_files[file_count].name, line, 63);
        doc_files[file_count].content[0] = '\0';
        
        FILE *fp = fopen(line, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (fsize > 0 && fsize < MAX_BUFFER / 2) {
                unsigned char *buf = malloc(fsize + 1);
                if (buf) {
                    fread(buf, 1, fsize, fp);
                    base64_encode(buf, fsize, doc_files[file_count].content);
                    free(buf);
                }
            }
            fclose(fp);
        }
        file_count++;
    }
    fclose(idx);
}

void send_to_client(client_t *client, const char *msg, int is_system, int is_game) {
    char out_buf[MAX_BUFFER + 256];
    if (is_system) {
        snprintf(out_buf, sizeof(out_buf), "%s[SYS] %s%s\n", COLOR_SYS, msg, COLOR_RESET);
    } else if (is_game) {
        snprintf(out_buf, sizeof(out_buf), "%s[GAME] %s%s\n", COLOR_GAME, msg, COLOR_RESET);
    } else {
        snprintf(out_buf, sizeof(out_buf), "%s\n", msg);
    }
    send_eth_frame(client->mac, MSG_DATA, out_buf, strlen(out_buf));
}

void broadcast_user_list() {
    char list_buf[MAX_BUFFER];
    strcpy(list_buf, "[LIST] ");
    client_t *curr = head;
    while (curr) {
        strcat(list_buf, curr->nickname);
        if (curr->next) strcat(list_buf, ", ");
        curr = curr->next;
    }
    unsigned char broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    send_eth_frame(broadcast_mac, MSG_DATA, list_buf, strlen(list_buf));
}

void broadcast_file_list() {
    char list_msg[MAX_BUFFER] = "[FILES] ";
    for (int i = 0; i < file_count; i++) {
        strcat(list_msg, doc_files[i].name);
        if (i < file_count - 1) strcat(list_msg, ",");
    }
    unsigned char broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    send_eth_frame(broadcast_mac, MSG_DATA, list_msg, strlen(list_msg));
}

void broadcast_message(client_t *sender, const char *msg, int is_system, int is_game, int is_raw) {
    client_t *curr = head;
    char out_buf[MAX_BUFFER + 256];

    if (is_raw) {
        snprintf(out_buf, sizeof(out_buf), "%s\n", msg);
    } else if (is_system) {
        snprintf(out_buf, sizeof(out_buf), "%s[SYS] %s%s\n", COLOR_SYS, msg, COLOR_RESET);
    } else if (is_game) {
        snprintf(out_buf, sizeof(out_buf), "%s[GAME] %s%s\n", COLOR_GAME, msg, COLOR_RESET);
    } else {
        snprintf(out_buf, sizeof(out_buf), "%s[%s]%s: %s\n", COLOR_USER, sender ? sender->nickname : "Server", COLOR_RESET, msg);
    }

    while (curr) {
        if (curr != sender) {
            send_eth_frame(curr->mac, MSG_DATA, out_buf, strlen(out_buf));
        }
        curr = curr->next;
    }
}

void remove_client(client_t *client) {
    if (client->prev) client->prev->next = client->next;
    else head = client->next;
    
    if (client->next) client->next->prev = client->prev;

    char sys_msg[128];
    snprintf(sys_msg, sizeof(sys_msg), "User '%s' left the chat.", client->nickname);
    printf("%s\n", sys_msg);
    broadcast_message(NULL, sys_msg, 1, 0, 0);
    trigger_user_list_broadcast();
    
    free(client);
}

void handle_command(client_t *client, char *cmd) {
    char sys_msg[MAX_BUFFER];
    
    if (strncmp(cmd, "/doc_sync ", 10) == 0) {
        char *payload = cmd + 10;
        char filename[64];
        char *first_pipe = strchr(payload, '|');
        if (first_pipe) {
            int name_len = first_pipe - payload;
            if (name_len > 63) name_len = 63;
            strncpy(filename, payload, name_len);
            filename[name_len] = '\0';
            
            for (int i = 0; i < file_count; i++) {
                if (strcmp(doc_files[i].name, filename) == 0) {
                    char *second_pipe = strchr(first_pipe + 1, '|');
                    if (second_pipe) {
                        save_file_to_disk(i, second_pipe + 1);
                    }
                    break;
                }
            }
        }
        char sync_msg[MAX_BUFFER];
        snprintf(sync_msg, sizeof(sync_msg), "[DOC_SYNC] %s|%s", client->nickname, payload);
        broadcast_message(client, sync_msg, 0, 0, 1);
        return;
    }

    if (strncmp(cmd, "/create_file ", 13) == 0) {
        char *filename = cmd + 13;
        if (file_count >= MAX_FILES) return;
        
        for(int i=0; i<file_count; i++) if(strcmp(doc_files[i].name, filename) == 0) return;
        
        strncpy(doc_files[file_count].name, filename, 63);
        doc_files[file_count].content[0] = '\0';
        file_count++;
        
        FILE *fp = fopen(filename, "w");
        if(fp) fclose(fp);
        
        update_files_index();
        broadcast_file_list();
        return;
    }

    if (strncmp(cmd, "/delete_file ", 13) == 0) {
        char *filename = cmd + 13;
        for (int i = 0; i < file_count; i++) {
            if (strcmp(doc_files[i].name, filename) == 0) {
                remove(filename);
                for (int j = i; j < file_count - 1; j++) {
                    doc_files[j] = doc_files[j + 1];
                }
                file_count--;
                update_files_index();
                broadcast_file_list();
                
                char del_msg[128];
                snprintf(del_msg, sizeof(del_msg), "File '%s' has been deleted.", filename);
                broadcast_message(NULL, del_msg, 1, 0, 0);
                break;
            }
        }
        return;
    }

    if (strncmp(cmd, "/nick ", 6) == 0) {
        char *new_nick = cmd + 6;
        while (*new_nick == ' ') new_nick++;
        if (strlen(new_nick) > 0) {
            char old_nick[NICKNAME_LEN];
            strcpy(old_nick, client->nickname);
            snprintf(client->nickname, NICKNAME_LEN, "%.31s", new_nick);
            snprintf(sys_msg, sizeof(sys_msg), "User '%s' is now known as '%s'", old_nick, client->nickname);
            broadcast_message(NULL, sys_msg, 1, 0, 0);
            trigger_user_list_broadcast(); 
        }
    } else if (strcmp(cmd, "/list") == 0) {
        strcpy(sys_msg, "Online users: ");
        client_t *curr = head;
        while (curr) {
            strcat(sys_msg, curr->nickname);
            if (curr->next) strcat(sys_msg, ", ");
            curr = curr->next;
        }
        send_to_client(client, sys_msg, 1, 0);
    } else if (strcmp(cmd, "/roll") == 0) {
        int roll = (rand() % 100) + 1;
        snprintf(sys_msg, sizeof(sys_msg), "%s rolled a %d (1-100)!", client->nickname, roll);
        broadcast_message(NULL, sys_msg, 0, 1, 0);
    } else if (strcmp(cmd, "/startgame") == 0) {
        if (game_active) {
            send_to_client(client, "Game is already active!", 0, 1);
        } else {
            game_active = 1;
            game_min = 1;
            game_max = 100;
            game_target = (rand() % 100) + 1;
            snprintf(sys_msg, sizeof(sys_msg), "%s started a new number guessing game! Guess a number between 1 and 100 using '/guess <num>'", client->nickname);
            broadcast_message(NULL, sys_msg, 0, 1, 0);
        }
    } else if (strncmp(cmd, "/guess ", 7) == 0) {
        if (!game_active) {
            send_to_client(client, "No active game. Type /startgame to start.", 0, 1);
            return;
        }
        int guess = atoi(cmd + 7);
        if (guess < 1 || guess > 100) {
            send_to_client(client, "Please guess a number between 1 and 100.", 0, 1);
            return;
        }

        snprintf(sys_msg, sizeof(sys_msg), "%s guessed %d...", client->nickname, guess);
        broadcast_message(NULL, sys_msg, 0, 1, 0);

        if (guess == game_target) {
            snprintf(sys_msg, sizeof(sys_msg), "🎉 BINGO! %s guessed the correct number: %d! Game Over.", client->nickname, game_target);
            broadcast_message(NULL, sys_msg, 0, 1, 0);
            game_active = 0;
        } else if (guess < game_target) {
            if (guess >= game_min) game_min = guess + 1;
            snprintf(sys_msg, sizeof(sys_msg), "Too low! The number is between %d and %d.", game_min, game_max);
            broadcast_message(NULL, sys_msg, 0, 1, 0);
        } else {
            if (guess <= game_max) game_max = guess - 1;
            snprintf(sys_msg, sizeof(sys_msg), "Too high! The number is between %d and %d.", game_min, game_max);
            broadcast_message(NULL, sys_msg, 0, 1, 0);
        }
    } else {
        send_to_client(client, "Unknown command. Available: /nick, /list, /roll, /startgame, /guess", 1, 0);
    }
}

void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    char *buffer = malloc(MAX_BUFFER + 64);
    if (!buffer) return;
    
    struct sockaddr_ll src_addr;
    socklen_t addr_len = sizeof(src_addr);
    
    int n_read = recvfrom(raw_socket_fd, buffer, MAX_BUFFER + 64, 0, (struct sockaddr*)&src_addr, &addr_len);
    if (n_read < 17) { free(buffer); return; } // 太短
    
    struct ethhdr *eh = (struct ethhdr *)buffer;
    if (ntohs(eh->h_proto) != CUSTOM_ETH_TYPE) { free(buffer); return; } // 不是我們的協議
    
    // 忽略自己發送的封包
    if (memcmp(eh->h_source, server_mac, 6) == 0) { free(buffer); return; }

    unsigned char *src_mac = eh->h_source;
    uint8_t msg_type = buffer[14];
    uint16_t payload_len;
    memcpy(&payload_len, buffer + 15, 2);
    payload_len = ntohs(payload_len);
    
    if (n_read < 17 + payload_len) { free(buffer); return; } // 不完整的封包
    
    // 透過 MAC 尋找客戶端
    client_t *client = head;
    while (client) {
        if (memcmp(client->mac, src_mac, 6) == 0) break;
        client = client->next;
    }
    
    if (msg_type == MSG_JOIN) {
        if (!client) {
            client = (client_t *)malloc(sizeof(client_t));
            memcpy(client->mac, src_mac, 6);
            client->has_joined = 1;
            client->ping_missed = 0;
            const char *animals[] = {"Fox", "Cat", "Dog", "Bear", "Owl", "Lion", "Wolf", "Frog", "Duck", "Seal", "Kiwi", "Panda", "Deer", "Tiger", "Koala", "Swan"};
            int num_animals = sizeof(animals) / sizeof(animals[0]);
            int idx = (src_mac[4] + src_mac[5]) % num_animals;
            snprintf(client->nickname, NICKNAME_LEN, "%s_%02X%02X", animals[idx], src_mac[4], src_mac[5]);
            
            client->prev = NULL;
            client->next = head;
            if (head) head->prev = client;
            head = client;
            
            send_to_client(client, "Welcome! Type /nick <name> to change your name.", 1, 0);
            
            broadcast_file_list();
            for (int i = 0; i < file_count; i++) {
                if (strlen(doc_files[i].content) > 0) {
                    char sync_msg[MAX_BUFFER + 128];
                    snprintf(sync_msg, sizeof(sync_msg), "[DOC_SYNC] Server|%s|0|%s\n", doc_files[i].name, doc_files[i].content);
                    send_eth_frame(client->mac, MSG_DATA, sync_msg, strlen(sync_msg));
                }
            }
            
            char sys_msg[128];
            snprintf(sys_msg, sizeof(sys_msg), "User '%s' joined the chat.", client->nickname);
            printf("%s\n", sys_msg);
            broadcast_message(client, sys_msg, 1, 0, 0);
            trigger_user_list_broadcast();
        } else {
            // 已存在的客戶端重新 JOIN (例如重新整理網頁)
            client->has_joined = 1;
            client->ping_missed = 0;
            trigger_user_list_broadcast();
            
            // 補發文件列表給他
            broadcast_file_list();
            for (int i = 0; i < file_count; i++) {
                if (strlen(doc_files[i].content) > 0) {
                    char sync_msg[MAX_BUFFER + 128];
                    snprintf(sync_msg, sizeof(sync_msg), "[DOC_SYNC] Server|%s|0|%s\n", doc_files[i].name, doc_files[i].content);
                    send_eth_frame(client->mac, MSG_DATA, sync_msg, strlen(sync_msg));
                }
            }
        }
        free(buffer);
        return;
    }
    
    if (!client) { free(buffer); return; } // 忽略未 JOIN 就發送資料的裝置
    
    if (msg_type == MSG_PONG) {
        client->ping_missed = 0;
        free(buffer);
        return;
    }
    
    if (msg_type == MSG_LEAVE) {
        remove_client(client);
        free(buffer);
        return;
    }
    
    if (msg_type == MSG_DATA) {
        char *payload = malloc(payload_len + 1);
        memcpy(payload, buffer + 17, payload_len);
        payload[payload_len] = '\0';
        
        char *newline;
        char *start_ptr = payload;
        
        while ((newline = strchr(start_ptr, '\n')) != NULL) {
            *newline = '\0';
            if (newline > start_ptr && *(newline - 1) == '\r') {
                *(newline - 1) = '\0';
            }
            
            if (strlen(start_ptr) > 0) {
                if (start_ptr[0] == '/') {
                    handle_command(client, start_ptr);
                } else {
                    broadcast_message(client, start_ptr, 0, 0, 0);
                }
            }
            start_ptr = newline + 1;
        }
        if (strlen(start_ptr) > 0) {
            if (start_ptr[0] == '/') {
                handle_command(client, start_ptr);
            } else {
                broadcast_message(client, start_ptr, 0, 0, 0);
            }
        }
        free(payload);
    }
    free(buffer);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface_name> (e.g. eth0 or lo)\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *if_name = argv[1];
    
    // 建立 Raw Socket (Layer 2)
    raw_socket_fd = socket(AF_PACKET, SOCK_RAW, htons(CUSTOM_ETH_TYPE));
    if (raw_socket_fd < 0) {
        perror("Socket creation failed (Did you run with sudo?)");
        exit(EXIT_FAILURE);
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
    
    if (ioctl(raw_socket_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("Failed to get interface index");
        exit(EXIT_FAILURE);
    }
    ifindex = ifr.ifr_ifindex;
    
    if (ioctl(raw_socket_fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("Failed to get MAC address");
        exit(EXIT_FAILURE);
    }
    memcpy(server_mac, ifr.ifr_hwaddr.sa_data, 6);
    printf("Server MAC: %02X:%02X:%02X:%02X:%02X:%02X on interface %s\n",
           server_mac[0], server_mac[1], server_mac[2],
           server_mac[3], server_mac[4], server_mac[5], if_name);
           
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(CUSTOM_ETH_TYPE);
    sll.sll_ifindex = ifindex;
    if (bind(raw_socket_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    int flags = fcntl(raw_socket_fd, F_GETFL);
    fcntl(raw_socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Increase receive buffer to 8MB to prevent drops during broadcast storms
    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(raw_socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    srand(time(NULL));
    init_files();
    struct ev_loop *loop = ev_default_loop(0);
    main_loop = loop;
    if (!loop) {
        perror("Failed to initialize libev loop");
        exit(EXIT_FAILURE);
    }
    
    ev_timer_init(&user_list_timer, user_list_timer_cb, 0., 0.);
    
    // Heartbeat check every 5.0 seconds
    ev_timer_init(&heartbeat_timer, heartbeat_timer_cb, 5.0, 5.0);
    ev_timer_start(loop, &heartbeat_timer);
    
    struct ev_io read_watcher;
    ev_io_init(&read_watcher, read_cb, raw_socket_fd, EV_READ);
    ev_io_start(loop, &read_watcher);
    
    printf("Co-edit Server listening for Custom Protocol 0x%04X on Raw Socket...\n", CUSTOM_ETH_TYPE);
    
    ev_loop(loop, 0);
    return 0;
}

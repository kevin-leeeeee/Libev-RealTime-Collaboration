#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ev.h>
#include <time.h>

#define PORT 8080
#define MAX_BUFFER 65536 // 加大 buffer 以支援共編長文章
#define NICKNAME_LEN 32

// ANSI Colors for Terminal
#define COLOR_RESET "\033[0m"
#define COLOR_SYS   "\033[1;33m"
#define COLOR_ERR   "\033[1;31m"
#define COLOR_USER  "\033[1;36m"
#define COLOR_GAME  "\033[1;32m"

typedef struct client_node {
    struct ev_io io;
    int fd;
    char nickname[NICKNAME_LEN];
    char read_buffer[MAX_BUFFER];
    int buffer_len;
    int has_joined;
    struct client_node *prev;
    struct client_node *next;
} client_t;

client_t *head = NULL;

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

int base64_encode(const char *input, char *output) {
    int i, j;
    int len = strlen(input);
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

void save_file_to_disk(int index, const char *base64_content) {
    if (index < 0 || index >= file_count) return;
    
    // 更新記憶體中的 Base64 內容
    strncpy(doc_files[index].content, base64_content, MAX_BUFFER - 1);
    doc_files[index].content[MAX_BUFFER - 1] = '\0';

    // 解碼後寫入磁碟
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

void init_files() {
    // 初始化檔案列表（目前暫無內容）
}
void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
void broadcast_message(client_t *sender, const char *msg, int is_system, int is_game, int is_raw);
void send_to_client(client_t *client, const char *msg, int is_system, int is_game);
void remove_client(struct ev_loop *loop, client_t *client);
void handle_command(client_t *client, char *cmd);
void setnonblock(int fd);
void broadcast_user_list(); // 新增名單廣播功能

void setnonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return;
    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
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
    send(client->fd, out_buf, strlen(out_buf), MSG_DONTWAIT);
}

// 廣播最新在線名單給所有人
void broadcast_user_list() {
    char list_buf[MAX_BUFFER];
    strcpy(list_buf, "[LIST] ");
    client_t *curr = head;
    while (curr) {
        strcat(list_buf, curr->nickname);
        if (curr->next) strcat(list_buf, ", ");
        curr = curr->next;
    }
    // 直接當作 raw message 廣播給所有人
    broadcast_message(NULL, list_buf, 0, 0, 1);
}

void broadcast_file_list() {
    char list_msg[MAX_BUFFER] = "[FILES] ";
    for (int i = 0; i < file_count; i++) {
        strcat(list_msg, doc_files[i].name);
        if (i < file_count - 1) strcat(list_msg, ",");
    }
    broadcast_message(NULL, list_msg, 0, 0, 1);
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
        snprintf(out_buf, sizeof(out_buf), "%s[%s]%s: %s\n", COLOR_USER, sender->nickname, COLOR_RESET, msg);
    }

    while (curr) {
        if (curr != sender) {
            send(curr->fd, out_buf, strlen(out_buf), MSG_DONTWAIT);
        }
        curr = curr->next;
    }
}

void handle_command(client_t *client, char *cmd) {
    char sys_msg[MAX_BUFFER];
    
    if (strncmp(cmd, "/__PROXY_CONNECT__", 18) == 0) {
        if (!client->has_joined) {
            client->has_joined = 1;
            char sys_msg[128];
            snprintf(sys_msg, sizeof(sys_msg), "User '%s' joined the chat.", client->nickname);
            printf("%s\n", sys_msg);
            broadcast_message(client, sys_msg, 1, 0, 0);
            broadcast_user_list();
        }
        return;
    }
    
    // 多檔案共編指令格式: /doc_sync 檔名|位置|資料
    if (strncmp(cmd, "/doc_sync ", 10) == 0) {
        char *payload = cmd + 10;
        
        // 解析檔名並更新伺服器記憶體狀態
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
                        if (client != NULL) {
                            save_file_to_disk(i, second_pipe + 1);
                        }
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
        
        // 檢查重複
        for(int i=0; i<file_count; i++) if(strcmp(doc_files[i].name, filename) == 0) return;
        
        strncpy(doc_files[file_count].name, filename, 63);
        doc_files[file_count].content[0] = '\0';
        file_count++;
        
        // 建立實體檔案
        FILE *fp = fopen(filename, "w");
        if(fp) fclose(fp);
        
        broadcast_file_list();
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
            broadcast_user_list(); // 名稱改變後推播新名單
        }
    } else if (strcmp(cmd, "/list") == 0) {
        // 向發送者顯示列表 (向下相容)
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

void remove_client(struct ev_loop *loop, client_t *client) {
    ev_io_stop(loop, &client->io);
    close(client->fd);

    if (client->prev) client->prev->next = client->next;
    else head = client->next;
    
    if (client->next) client->next->prev = client->prev;

    char sys_msg[128];
    if (strcmp(client->nickname, "__PROBE__") != 0) {
        snprintf(sys_msg, sizeof(sys_msg), "User '%s' left the chat.", client->nickname);
        printf("%s\n", sys_msg);
        broadcast_message(NULL, sys_msg, 1, 0, 0);
        broadcast_user_list();
    }
    
    free(client);
}

void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    client_t *client = (client_t *)watcher;
    
    int remaining_space = MAX_BUFFER - client->buffer_len - 1;
    if (remaining_space <= 0) {
        client->buffer_len = 0;
        remaining_space = MAX_BUFFER - 1;
    }

    int n_read = recv(client->fd, client->read_buffer + client->buffer_len, remaining_space, 0);

    if (n_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        remove_client(loop, client);
        return;
    } else if (n_read == 0) {
        remove_client(loop, client);
        return;
    }

    client->buffer_len += n_read;
    client->read_buffer[client->buffer_len] = '\0';

    char *newline;
    char *start_ptr = client->read_buffer;

    while ((newline = strchr(start_ptr, '\n')) != NULL) {
        *newline = '\0';
        if (newline > start_ptr && *(newline - 1) == '\r') {
            *(newline - 1) = '\0';
        }

        if (strlen(start_ptr) > 0) {
            if (strncmp(start_ptr, "GET ", 4) == 0 || strncmp(start_ptr, "HEAD ", 5) == 0 || strncmp(start_ptr, "POST ", 5) == 0 || strncmp(start_ptr, "Host: ", 6) == 0) {
                // 偵測到 HTTP 請求 (Render Health Check)，標記為探測並斷開連線
                printf("Ignored HTTP probe and disconnected: %s\n", start_ptr);
                strcpy(client->nickname, "__PROBE__"); // 特殊標記，讓 remove_client 不廣播離開訊息
                remove_client(loop, client);
                return;
            } else if (start_ptr[0] == '/') {
                handle_command(client, start_ptr);
            } else {
                broadcast_message(client, start_ptr, 0, 0, 0);
            }
        }
        start_ptr = newline + 1;
    }

    int remaining = client->read_buffer + client->buffer_len - start_ptr;
    if (remaining > 0 && start_ptr != client->read_buffer) {
        memmove(client->read_buffer, start_ptr, remaining);
        client->buffer_len = remaining;
    } else if (remaining == 0) {
        client->buffer_len = 0;
    }
}

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) return;

    setnonblock(client_fd);

    client_t *new_client = (client_t *)malloc(sizeof(client_t));
    if (!new_client) {
        close(client_fd);
        return;
    }

    new_client->fd = client_fd;
    new_client->buffer_len = 0;
    new_client->has_joined = 0;
    memset(new_client->read_buffer, 0, MAX_BUFFER);
    snprintf(new_client->nickname, NICKNAME_LEN, "Guest_%d", client_fd);

    new_client->prev = NULL;
    new_client->next = head;
    if (head) head->prev = new_client;
    head = new_client;

    ev_io_init(&new_client->io, read_cb, client_fd, EV_READ);
    ev_io_start(loop, &new_client->io);

    send_to_client(new_client, "Welcome! Type /nick <name> to change your name.", 1, 0);
    
    // 新連線時，同步檔案列表與各檔案內容
    broadcast_file_list();
    for (int i = 0; i < file_count; i++) {
        if (strlen(doc_files[i].content) > 0) {
            char sync_msg[MAX_BUFFER + 128];
            snprintf(sync_msg, sizeof(sync_msg), "[DOC_SYNC] Server|%s|0|%s\n", doc_files[i].name, doc_files[i].content);
            send(new_client->fd, sync_msg, strlen(sync_msg), MSG_DONTWAIT);
        }
    }
}

#include <signal.h>

int main() {
    signal(SIGPIPE, SIG_IGN);
    srand(time(NULL));
    init_files();
    struct ev_loop *loop = ev_default_loop(0);
    if (!loop) {
        perror("Failed to initialize libev loop");
        exit(EXIT_FAILURE);
    }
    
    int server_fd;
    struct sockaddr_in server_addr;
    struct ev_io accept_watcher;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket error"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { perror("bind error"); exit(EXIT_FAILURE); }
    if (listen(server_fd, SOMAXCONN) < 0) { perror("listen error"); exit(EXIT_FAILURE); }

    setnonblock(server_fd);
    printf("Co-edit Server listening on port %d...\n", PORT);
    fflush(stdout); // 確保印出，不被 buffer 吃掉

    ev_io_init(&accept_watcher, accept_cb, server_fd, EV_READ);
    ev_io_start(loop, &accept_watcher);

    ev_loop(loop, 0);

    return 0;
}

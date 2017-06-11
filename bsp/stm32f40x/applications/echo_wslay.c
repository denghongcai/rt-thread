#include "echo_wslay.h"

#include "html.h"

#define WSLAY_DEBUG
#define ssize_t int
#define BASE64_ENCODE_RAW_LENGTH(length) ((((length) + 2)/3)*4)
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define MYPORT 1234    // the port users will be connecting to
#define BACKLOG 5     // how many pending connections queue will hold
#define BUF_SIZE 1024

const char* status = "HTTP/1.1 200 OK\r\n";
const char* connection  = "Connection: close\r\n";
const char* contentLength  = "Content-Length: 598\r\n\r\n";

int conn_amount;      // current connection amount

enum CONN_STATE {
    CONN_HTTP = 0,
    CONN_WS
};

struct Client {
    int fd;
    http_parser* parser;
    http_parser_settings parserSettings;
    char wsKey[29];
    wslay_event_context_ptr wsCtx;
    int state;
};

struct Client clients[BACKLOG];

int write(int fd, const char* buf, size_t bufsize) {
    int sent_len = 0;
    int total_len = bufsize;
    while(sent_len < total_len) {
        int ret = send(fd, buf + sent_len, bufsize - sent_len, 0);
        if (ret < 0) {
            return ret;
        }
        sent_len += ret;
    }
    return 0;
}

int read(int fd, char* buf, size_t bufsize) {
    int ret;
    while(RT_TRUE) {
        ret = recv(fd, buf, bufsize, MSG_DONTWAIT);
        if (errno == EAGAIN) {
            continue;
        }
        break;
    }
    return ret;
}

ssize_t send_callback(wslay_event_context_ptr ctx,
                      const uint8_t *data, size_t len, int flags,
                      void *user_data)
{
    struct Client *client = (struct Client*)user_data;
    ssize_t r;
    int sflags = 0;
#ifdef MSG_MORE
    if(flags & WSLAY_MSG_MORE) {
        sflags |= MSG_MORE;
    }
#endif // MSG_MORE
    r = send(client->fd, data, len, 0);
    if(r == -1) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    }
    return r;
}

ssize_t recv_callback(wslay_event_context_ptr ctx, uint8_t *buf, size_t len,
                      int flags, void *user_data)
{
    struct Client *client = (struct Client*)user_data;
    ssize_t r;
    r = recv(client->fd, buf, len, MSG_DONTWAIT);
    if(r == -1) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    } else if(r == 0) {
        /* Unexpected EOF is also treated as an error */
        wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        r = -1;
    }
    return r;
}

void on_msg_recv_callback(wslay_event_context_ptr ctx,
                          const struct wslay_event_on_msg_recv_arg *arg,
                          void *user_data)
{
    /* Echo back non-control message */
    if(!wslay_is_ctrl_frame(arg->opcode)) {
        struct wslay_event_msg msgarg = {
            arg->opcode, arg->msg, arg->msg_length
        };
        wslay_event_queue_msg(ctx, &msgarg);
#ifdef WSLAY_DEBUG
        rt_kprintf("ws msg: %.*s\n", arg->msg_length, arg->msg);
#endif
    }
}

void showclient()
{
    int i;
    rt_kprintf("client amount: %d\n", conn_amount);
    for (i = 0; i < BACKLOG; i++) {
        rt_kprintf("[%d]:%d  ", i, clients[i].fd);
    }
    rt_kprintf("\n\n");
}

/*
 * Create Server's accept key in *dst*.
 * *client_key* is the value of |Sec-WebSocket-Key| header field in
 * client's handshake and it must be length of 24.
 * *dst* must be at least BASE64_ENCODE_RAW_LENGTH(20)+1.
 */
void create_accept_key(char *dst, const char *client_key)
{
    uint8_t sha1buf[20], key_src[60];
    memcpy(key_src, client_key, 24);
    memcpy(key_src+24, WS_GUID, 36);
    sha1(key_src, sizeof(key_src), sha1buf);
    Base64encode(dst, (const char*)sha1buf, 20);
    dst[BASE64_ENCODE_RAW_LENGTH(20)] = '\0';
}


int header_field_callback(http_parser* parser, const char *at, size_t length) {
#ifdef WSLAY_DEBUG
    rt_kprintf("header_field: %.*s\n", length, at);
#endif
    if (rt_strncmp(at, "Sec-WebSocket-Key", length) == 0) {
        struct Client* client = parser->data;
        rt_memcpy(client->wsKey, at, length);
    }
    return 0;
}

int header_value_callback(http_parser* parser, const char *at, size_t length) {
#ifdef WSLAY_DEBUG
    rt_kprintf("header_value: %.*s\n", length, at);
#endif
    struct Client* client = parser->data;
    if (rt_strncmp(client->wsKey, "Sec-WebSocket-Key", 17) == 0) {
#ifdef WSLAY_DEBUG
        rt_kprintf("Sec-WebSocket-Key: %.*s\n", length, at);
#endif
        rt_memset(client->wsKey, '\0', 29);
        create_accept_key(client->wsKey, at);
#ifdef WSLAY_DEBUG
        rt_kprintf("Sec-WebSocket-Accept: %.*s\n", 29, client->wsKey);
#endif
    }
    return 0;
}

int show_url_callback(http_parser* parser, const char *at, size_t length) {
    /* access to thread local custom_data_t struct.
    Use this access save parsed data for later use into thread local
    buffer, or communicate over socket
    */
#ifdef WSLAY_DEBUG
    rt_kprintf("url: %.*s\n", length, at);
#endif
    return 0;
}

int send_body_callback(http_parser* parser) {
#ifdef WSLAY_DEBUG
    rt_kprintf("header complete\n");
#endif
    struct Client* client = parser->data;
    if (parser->upgrade) {
        char res_header[256];
        rt_snprintf(res_header, sizeof(res_header),
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n"
                    "\r\n", client->wsKey);
        write(client->fd, res_header, rt_strlen(res_header));
        struct wslay_event_callbacks callbacks = {
            recv_callback,
            send_callback,
            NULL,
            NULL,
            NULL,
            NULL,
            on_msg_recv_callback
        };
        int ret = wslay_event_context_server_init(&client->wsCtx, &callbacks, client);
#ifdef WSLAY_DEBUG
        rt_kprintf("wslay_event_context_server_init ret: %d\n", ret);
#endif
        client->state = CONN_WS;
    } else {
        write(client->fd, status, 17);
        write(client->fd, connection, 19);
        write(client->fd, contentLength, 23);
        write(client->fd, index_html, index_html_len);
    }
    return 0;
}

int init_client(struct Client* client) {
    client->state = CONN_HTTP;
    client->parserSettings.on_url = show_url_callback;
    client->parserSettings.on_header_field = header_field_callback;
    client->parserSettings.on_header_value = header_value_callback;
    client->parserSettings.on_headers_complete = send_body_callback;
    client->parser = rt_malloc(sizeof(http_parser));
    http_parser_init(client->parser, HTTP_REQUEST);
    client->parser->data = client;
    return 0;
}

int destroy_client(struct Client* client) {
    if (client->parser != NULL) {
        rt_free(client->parser);
    }
    return 0;
}

void wslay_server()
{
    int sock_fd, new_fd;             // listen on sock_fd, new connection on new_fd
    struct sockaddr_in server_addr;  // server address information
    struct sockaddr_in client_addr;  // connector's address information

    socklen_t sin_size;
    int yes = 1;
    char buf[BUF_SIZE];
    int ret;
    int i;

    if ((sock_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        exit(1);
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == -1) {
        exit(1);
    }

    server_addr.sin_family = AF_INET;         // host byte order
    server_addr.sin_port = htons(MYPORT);     // short, network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        exit(1);
    }

    if (listen(sock_fd, BACKLOG) == -1) {
        exit(1);
    }
    rt_kprintf("listen port %d\n", MYPORT);
    fd_set fdsr;
    int maxsock;
    struct timeval tv;
    conn_amount = 0;
    sin_size = sizeof(client_addr);
    maxsock = sock_fd;
    while (1)
    {
        // initialize file descriptor set
        FD_ZERO(&fdsr);
        FD_SET(sock_fd, &fdsr);  // add fd
        // timeout setting
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        // add active connection to fd set
        for (i = 0; i < BACKLOG; i++) {
            if (clients[i].fd != 0) {
                FD_SET(clients[i].fd, &fdsr);
            }
        }
        ret = select(maxsock + 1, &fdsr, NULL, NULL, &tv);
        if (ret < 0) {          // error
            break;
        } else if (ret == 0) {  // time out
#ifdef WSLAY_DEBUG
            rt_kprintf("select timeout\n");
#endif
            continue;
        }
        // check every fd in the set
        for (i = 0; i < conn_amount; i++)
        {
            if (FD_ISSET(clients[i].fd, &fdsr)) // check which fd is ready
            {
                int ret = 0;
                if (clients[i].state == CONN_HTTP) {
                    ret = read(clients[i].fd, buf, sizeof(buf));

                    if (ret >= 0) {
                        if (ret != 0) {
                            // rt_kprintf("client[%d]'s buf: %s\n", i, buf);
                        }
                        int nparsed = http_parser_execute(clients[i].parser, &clients[i].parserSettings, buf, ret);
#ifdef WSLAY_DEBUG
                        rt_kprintf("client[%d]'s http method: %s\n", i, http_method_str(clients[i].parser->method));
#endif
                        if (clients[i].parser->upgrade) {
                            /* handle new protocol */
                        } else if (nparsed != ret) {
                            /* Handle error. Usually just close the connection. */
#ifdef WSLAY_DEBUG
                            rt_kprintf("client[%d]'s ret : %d , nparsed : %d\n", i, ret, nparsed);
#endif
                            ret = 0;
                        }
                    }
                } else {
                    if (wslay_event_want_read(clients[i].wsCtx) || wslay_event_want_write(clients[i].wsCtx)) {
                        if (wslay_event_want_read(clients[i].wsCtx)) {
                            ret = wslay_event_recv(clients[i].wsCtx);
                        }
                        if (wslay_event_want_write(clients[i].wsCtx)) {
                            ret = wslay_event_send(clients[i].wsCtx);
                        }
                        if (wslay_event_want_read(clients[i].wsCtx) || wslay_event_want_write(clients[i].wsCtx)) {
                            continue;
                        }
                    } else {
                        ret = 0;
                    }
                }
                if (ret <= 0) {
#ifdef WSLAY_DEBUG
                    rt_kprintf("ret : %d and client[%d] close\n", ret, i);
#endif
                    closesocket(clients[i].fd);
                    FD_CLR(clients[i].fd, &fdsr);  // delete fd
                    clients[i].fd = 0;
                    destroy_client(&clients[i]);
                    conn_amount--;
                    struct Client t = clients[conn_amount];
                    clients[conn_amount] = clients[i];
                    clients[i] = t;
                }
            }
        }
        // check whether a new connection comes
        if (FD_ISSET(sock_fd, &fdsr))  // accept new connection
        {
            new_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &sin_size);
            if (new_fd <= 0)
            {
                continue;
            }
            // add to fd queue
            if (conn_amount < BACKLOG)
            {
                clients[conn_amount++].fd = new_fd;
                rt_kprintf("new connection client[%d] %s:%d\n", conn_amount,
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                if (new_fd > maxsock)  // update the maxsock fd for select function
                    maxsock = new_fd;

                // init client
                init_client(&clients[conn_amount - 1]);
            }
            else
            {
                rt_kprintf("max concurrent connections arrive\n");
                closesocket(new_fd);
            }
        }
#ifdef WSLAY_DEBUG
        showclient();
#endif
    }
    // close other connections
    for (i = 0; i < BACKLOG; i++)
    {
        if (clients[i].fd != 0)
        {
            closesocket(clients[i].fd);
        }
    }
    exit(0);
}


#ifdef RT_USING_FINSH
#include <finsh.h>
FINSH_FUNCTION_EXPORT(wslay_server, startup wslay server);
#endif


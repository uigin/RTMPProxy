#ifndef __RTMP_PROXY_H__
#define __RTMP_PROXY_H__

typedef struct RTMP_PROXY
{
  char *server_ip;
  int server_port;
  int running;
  int listen_socket;
} RTMP_PROXY;

typedef struct RTMP_PROXY_SESSION
{
  RTMP *server_rtmp;
  RTMP *client_rtmp;
  AVal app;
  AVal stream;
  pthread_t work_pt;
  int client_socket;
  int work_mode;

  struct RTMP_PROXY_SESSION *prev;
  struct RTMP_PROXY_SESSION *next;
} RTMP_PROXY_SESSION;

typedef struct RTMP_PROXY_SESSION_ADMIN
{
  int session_num;
  RTMP_PROXY_SESSION *start;
} RTMP_PROXY_SESSION_ADMIN;

#define SAVC(x) static const AVal av_##x = AVC(#x)
SAVC(app);
SAVC(connect);
SAVC(flashVer);
SAVC(swfUrl);
SAVC(pageUrl);
SAVC(tcUrl);
SAVC(fpad);
SAVC(capabilities);
SAVC(audioCodecs);
SAVC(videoCodecs);
SAVC(_result);
SAVC(createStream);
SAVC(getStreamLength);
SAVC(play);
SAVC(fmsVer);
SAVC(mode);
SAVC(level);
SAVC(code);
SAVC(description);
SAVC(secureToken);
SAVC(type);
SAVC(nonprivate);
SAVC(releaseStream);
SAVC(FCPublish);
SAVC(publish);
SAVC(onFCPublish);
SAVC(onStatus);
SAVC(clientid);
SAVC(onBWDone);
SAVC(FCUnpublish);
SAVC(deleteStream);

#define RTMP_PROXY_LISTEN_PORT 1935
#define SOCKET_LISTEN_QUEUE_LEN 10

#define RTMP_CONNECT_TIMEOUT 5
#define MODE_PUBLISH 1
#define MODE_PLAY 0
#define SERVER_CLIENT_CHUNK_SIZE 60000

int rtmp_proxy_init();
void rtmp_proxy_run();
void rtmp_proxy_close(RTMP_PROXY_SESSION *psession);
void rtmp_server_connect(RTMP_PROXY_SESSION *psession, int mode, int chunk_size);
void *rtmp_tunnel_establish(void *param);
void rtmp_proxy_client_handler(RTMP_PROXY_SESSION *psession, RTMPPacket *packet);
void rtmp_proxy_client_invoke(RTMP_PROXY_SESSION *psession, RTMPPacket *packet, int is_AMF3);
void rtmp_proxy_change_chunk_size(RTMP *r, const RTMPPacket *packet);

int RTMP_SendChunkSize(RTMP *r, int chunk_size);
int RTMP_SendResult(RTMP *r, int txn);
int RTMP_SendPlayStreamBegin(RTMP *r);
int RTMP_SendOnStatus(RTMP *r, const AVal *code_prop_val, int stream_id);
int RTMP_SendSampleAccess(RTMP *r, int stream_id);

int method_conect_handler(RTMP_PROXY_SESSION *psession, AMFObject obj);
int method_releaseStream_handler(RTMP_PROXY_SESSION *psession, AMFObject obj);
int method_FCPublish_handler(RTMP_PROXY_SESSION *psession, AMFObject obj);
int method_createStream_handler(RTMP_PROXY_SESSION *psession, AMFObject obj);
int method_publish_handler(RTMP_PROXY_SESSION *psession, AMFObject obj, int stream_id);
int method_FCUnpublish_handler(RTMP_PROXY_SESSION *psession);
int method_play_handler(RTMP_PROXY_SESSION *psession, AMFObject obj, int stream_id);

int proxy_session_init(RTMP_PROXY_SESSION *psession);
int proxy_session_destroy(RTMP_PROXY_SESSION *psession);
int proxy_session_admin_init(RTMP_PROXY_SESSION_ADMIN *padmin);
int proxy_session_admin_add(RTMP_PROXY_SESSION_ADMIN *padmin, RTMP_PROXY_SESSION *psession);
int proxy_session_admin_remove(RTMP_PROXY_SESSION_ADMIN *padmin, RTMP_PROXY_SESSION *psession);

#endif
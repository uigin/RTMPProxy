#include <stdio.h>
#include <librtmp/rtmp.h>
#include <librtmp/log.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include "rtmp_proxy.h"

RTMP_PROXY_SESSION_ADMIN session_admin;
RTMP_PROXY app;

int main(int argc, char *argv[])
{

    if (argc < 3)
    {
        printf("\n please run this application like this: ./rtmp_proxy <Server IP> <Server_Port>\n\n");
        return -1;
    }
    app.server_ip = argv[1];
    app.server_port = atoi(argv[2]);
    app.running = 1;
    app.listen_socket = rtmp_proxy_init();
    if (app.listen_socket < 0)
    {
        return -1;
    }

    printf("Target RTMP Server: %s:%d\n", app.server_ip, app.server_port);
    session_admin.session_num = 0;
    session_admin.start = NULL;

    rtmp_proxy_run();
    return 0;
}

int rtmp_proxy_init()
{
    int opt = 1;
    int proxy_server_socket = -1;
    //设置一个socket地址结构server_addr,代表服务器internet地址, 端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); //把一段内存区的内容全部设置为0
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(RTMP_PROXY_LISTEN_PORT);

    //创建用于internet的流协议(TCP)socket,用server_socket代表服务器socket
    proxy_server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (proxy_server_socket < 0)
    {
        printf("[%s]-%d: Create Server Socket Failed \n", __FUNCTION__, __LINE__);
        return -1;
    }
    setsockopt(proxy_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //把socket和socket地址结构联系起来
    if (bind(proxy_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        printf("[%s]-%d: Bind Server Socket Failed \n", __FUNCTION__, __LINE__);
        return -1;
    }

    // proxy_server_socket
    if (listen(proxy_server_socket, SOCKET_LISTEN_QUEUE_LEN))
    {
        printf("[%s]-%d: Listen Server Socket Failed \n", __FUNCTION__, __LINE__);
        return -1;
    }

    return proxy_server_socket;
}

void rtmp_server_connect(RTMP_PROXY_SESSION *psession, int mode, int chunk_size)
{
    int len = 0;
    char url[512] = {0};

    if (psession->server_rtmp != NULL)
    {
        RTMP_Close(psession->server_rtmp);
        RTMP_Free(psession->server_rtmp);
    }

    psession->server_rtmp = RTMP_Alloc();
    RTMP_Init(psession->server_rtmp);

    psession->server_rtmp->Link.timeout = 5;
    len = sprintf(url, "rtmp://%s:%d/%s/%s", app.server_ip, app.server_port, psession->app.av_val, psession->stream.av_val);
    if (mode == MODE_PUBLISH)
    {
        url[len - 1] = '\0';
    }
    if (!RTMP_SetupURL(psession->server_rtmp, url))
    {
        printf("SetupURL Err\n");
        goto EXIT;
    }

    // if unable,the AMF command would be 'play' instead of 'publish'
    if (mode == MODE_PUBLISH)
    {
        RTMP_EnableWrite(psession->server_rtmp);
    }

    if (!RTMP_Connect(psession->server_rtmp, NULL))
    {
        printf("Connect Err\n");
        goto EXIT;
    }

    if (mode == MODE_PUBLISH)
    {
        RTMP_SendChunkSize(psession->server_rtmp, chunk_size);
    }

    if (!RTMP_ConnectStream(psession->server_rtmp, 0))
    {
        printf("ConnectStream Err\n");
        goto EXIT2;
    }
    else
    {
        return;
    }
EXIT2:
    RTMP_Close(psession->server_rtmp);
EXIT:
    RTMP_Free(psession->server_rtmp);
    psession->server_rtmp = NULL;
}

void rtmp_proxy_run()
{
    int client_socket = -1;
    struct sockaddr_in client_addr;
    int client_addr_size = sizeof(client_addr);
    session_admin.session_num = 0;
    session_admin.start = NULL;
    while (app.running)
    {
        RTMP_PROXY_SESSION session;

        client_socket = accept(app.listen_socket, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_size);
        if (client_socket < 0)
        {
            printf("[%s]-%d: Server Socket Accept Failed, %s\n", __FUNCTION__, __LINE__, strerror(errno));
            continue;
        }
        printf("Client %s:%d connected!\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        proxy_session_init(&session);
        session.client_socket = client_socket;
        pthread_create(&(session.work_pt), 0, rtmp_tunnel_establish, &session);
    }
}

void rtmp_proxy_close(RTMP_PROXY_SESSION *psession)
{
    RTMP_Close(psession->server_rtmp);

    close(psession->client_rtmp->m_sb.sb_socket);
    psession->client_rtmp->m_sb.sb_socket = -1;
}

void *rtmp_tunnel_establish(void *param)
{
    int res = 0;
    RTMP_PROXY_SESSION *psession = (RTMP_PROXY_SESSION *)param;
    RTMPPacket packet = {0};

    psession->client_rtmp = RTMP_Alloc();
    RTMP_Init(psession->client_rtmp);
    psession->client_rtmp->m_sb.sb_socket = psession->client_socket;
    res = RTMP_Serve(psession->client_rtmp);
    if (res <= 0)
    {
        return NULL;
    }
    proxy_session_admin_add(&session_admin, psession);
    while (RTMP_IsConnected(psession->client_rtmp))
    {
        if (psession->work_mode == MODE_PUBLISH)
        {
            res = RTMP_ReadPacket(psession->client_rtmp, &packet);
        }
        else
        {
            if (!RTMP_IsConnected(psession->server_rtmp))
            {
                rtmp_server_connect(psession, MODE_PLAY, 0);
            }
            res = RTMP_ReadPacket(psession->server_rtmp, &packet);
        }

        if (res == 0)
        {
            RTMPPacket_Free(&packet);
            continue;
        }
        if (!RTMPPacket_IsReady(&packet))
        {
            continue;
        }
        rtmp_proxy_client_handler(psession, &packet);
    }
    printf("client disconnected\n");
    proxy_session_admin_remove(&session_admin, psession);
}

void rtmp_proxy_client_handler(RTMP_PROXY_SESSION *psession, RTMPPacket *packet)
{
    int res = 0;
    switch (packet->m_packetType)
    {
    case RTMP_PACKET_TYPE_CHUNK_SIZE:
        rtmp_proxy_change_chunk_size(psession->client_rtmp, packet);
        RTMPPacket_Free(packet);
        return;
    case RTMP_PACKET_TYPE_CONTROL:
    case RTMP_PACKET_TYPE_SERVER_BW:
        RTMPPacket_Free(packet);
        return;
    case RTMP_PACKET_TYPE_FLEX_MESSAGE:
        rtmp_proxy_client_invoke(psession, packet, 1);
        RTMPPacket_Free(packet);
        return;
    case RTMP_PACKET_TYPE_INVOKE:
        rtmp_proxy_client_invoke(psession, packet, 0);
        RTMPPacket_Free(packet);
        return;
    case RTMP_PACKET_TYPE_AUDIO:
    case RTMP_PACKET_TYPE_VIDEO:
    case RTMP_PACKET_TYPE_INFO:
        if (psession->work_mode == MODE_PUBLISH)
        {
            if (!RTMP_IsConnected(psession->server_rtmp))
            {
                rtmp_server_connect(psession, MODE_PUBLISH, psession->client_rtmp->m_inChunkSize);
            }
            res = RTMP_SendPacket(psession->server_rtmp, packet, FALSE);
        }
        else
        {
            if (RTMP_IsConnected(psession->client_rtmp))
            {
                RTMP_SendPacket(psession->client_rtmp, packet, FALSE);
            }
        }
        RTMPPacket_Free(packet);
        return;
    default:
        RTMPPacket_Free(packet);
        return;
    }
}

void rtmp_proxy_client_invoke(RTMP_PROXY_SESSION *psession, RTMPPacket *packet, int is_AMF3)
{
    int res = 0;
    int i = 0;
    int n_body_size = 0;
    char *body = NULL;
    AVal method;
    AMFObject obj;

    n_body_size = packet->m_nBodySize;
    if (is_AMF3) // the first byte in the body of AMF3 is useless
    {
        body = packet->m_body;
        n_body_size--;
    }
    else
    {
        body = packet->m_body;
    }
    res = AMF_Decode(&obj, body, n_body_size, FALSE);
    if (res < 0)
    {
        printf("[%s]-%d: AMF Decoded Failed with %d\n", __FUNCTION__, __LINE__, res);
        return;
    }
    AMF_Dump(&obj);
    AMFProp_GetString(AMF_GetProp(&obj, NULL, 0), &method);
    // printf("\t [%s]-%d: Invoke method %s\n", __FUNCTION__, __LINE__, method.av_val);
    if (AVMATCH(&method, &av_connect))
    {
        method_conect_handler(psession, obj);
    }
    else if (AVMATCH(&method, &av_releaseStream))
    {
        method_releaseStream_handler(psession, obj);
    }
    else if (AVMATCH(&method, &av_FCPublish))
    {
        method_FCPublish_handler(psession, obj);
    }
    else if (AVMATCH(&method, &av_createStream))
    {
        method_createStream_handler(psession, obj);
    }
    else if (AVMATCH(&method, &av_publish))
    {
        method_publish_handler(psession, obj, packet->m_nInfoField2);
    }
    else if (AVMATCH(&method, &av_FCUnpublish))
    {
        method_FCUnpublish_handler(psession);
    }
    else if (AVMATCH(&method, &av_deleteStream))
    {
    }

    if (AVMATCH(&method, &av_play))
    {
        method_play_handler(psession, obj, packet->m_nInfoField2);
    }
    else if (AVMATCH(&method, &av_getStreamLength))
    {
    }
}

int RTMP_SendChunkSize(RTMP *r, int chunk_size)
{
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    // r->m_inChunkSize = chunk_size;
    r->m_outChunkSize = chunk_size;

    packet.m_nChannel = 0x02; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_CHUNK_SIZE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeInt32(packet.m_body, pend, chunk_size);
    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

int RTMP_SendOnBWDone(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av_onBWDone);
    enc = AMF_EncodeNumber(enc, pend, 0);
    *enc++ = AMF_NULL;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

int RTMP_SendResult(RTMP *r, int txn)
{
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, txn);
    *enc++ = AMF_NULL;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

int RTMP_SendOnFCPublish(RTMP *r, int stream_id)
{
    AMFObjectProperty code_prop;
    AMFObjectProperty desc_prop;
    AVal code_prop_val;
    AVal desc_prop_val;
    AMFObject obj;
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x05; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = stream_id;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av_onFCPublish);
    enc = AMF_EncodeNumber(enc, pend, 0);
    *enc++ = AMF_NULL;

    code_prop.p_name = av_code;
    code_prop.p_type = AMF_STRING;
    code_prop_val.av_val = "NetStream.Publish.Start";
    code_prop_val.av_len = strlen(code_prop_val.av_val);
    code_prop.p_vu.p_aval = code_prop_val;

    desc_prop.p_name = av_description;
    desc_prop.p_type = AMF_STRING;
    desc_prop_val.av_val = "Started publishing stream.";
    desc_prop_val.av_len = strlen(desc_prop_val.av_val);
    desc_prop.p_vu.p_aval = desc_prop_val;

    obj.o_num = 2;
    obj.o_props = malloc(obj.o_num * sizeof(AMFObjectProperty));
    memcpy(obj.o_props, &code_prop, sizeof(AMFObjectProperty));
    memcpy(obj.o_props + 1, &desc_prop, sizeof(AMFObjectProperty));

    enc = AMF_Encode(&obj, enc, pend);
    packet.m_nBodySize = enc - packet.m_body;
    free(obj.o_props);

    RTMP_SendPacket(r, &packet, FALSE);
    return 0;
}

int RTMP_SendOnStatus(RTMP *r, const AVal *code_prop_val, int stream_id)
{
    AMFObjectProperty level_prop;
    AMFObjectProperty code_prop;
    AMFObjectProperty desc_prop;
    AMFObjectProperty clientid_prop;

    AVal level_prop_val;
    AVal desc_prop_val;
    AVal clientid_prop_val;

    AMFObject obj;
    RTMPPacket packet;
    char pbuf[1024], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x05; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = stream_id;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av_onStatus);
    enc = AMF_EncodeNumber(enc, pend, 0);
    *enc++ = AMF_NULL;

    level_prop.p_name = av_level;
    level_prop.p_type = AMF_STRING;
    level_prop_val.av_val = "status";
    level_prop_val.av_len = strlen(level_prop_val.av_val);
    level_prop.p_vu.p_aval = level_prop_val;

    code_prop.p_name = av_code;
    code_prop.p_type = AMF_STRING;
    code_prop.p_vu.p_aval = *code_prop_val;

    obj.o_num = 2;
    obj.o_props = malloc(obj.o_num * sizeof(AMFObjectProperty));
    memcpy(obj.o_props, &level_prop, sizeof(AMFObjectProperty));
    memcpy(obj.o_props + 1, &code_prop, sizeof(AMFObjectProperty));

    enc = AMF_Encode(&obj, enc, pend);
    packet.m_nBodySize = enc - packet.m_body;
    free(obj.o_props);

    RTMP_SendPacket(r, &packet, FALSE);
    return 0;
}

int RTMP_SendPlayStreamBegin(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x02; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_CONTROL;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeNumber(enc, pend, 0);

    packet.m_nBodySize = 6;
    return RTMP_SendPacket(r, &packet, FALSE);
}

int RTMP_SendSampleAccess(RTMP *r, int stream_id)
{
    RTMPPacket packet;
    AVal access_prop;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x05; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INFO;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    access_prop.av_val = "|RtmpSampleAccess";
    access_prop.av_len = strlen(access_prop.av_val);

    enc = AMF_EncodeString(enc, pend, &access_prop);
    enc = AMF_EncodeBoolean(enc, pend, TRUE);
    enc = AMF_EncodeBoolean(enc, pend, TRUE);

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

int method_conect_handler(RTMP_PROXY_SESSION *psession, AMFObject obj)
{
    int i;
    AMFObject cobj;
    AVal pname, pval;
    AMFProp_GetObject(AMF_GetProp(&obj, NULL, 2), &cobj);
    for (i = 0; i < cobj.o_num; i++)
    {
        pname = cobj.o_props[i].p_name;
        pval.av_val = NULL;
        pval.av_len = 0;
        if (cobj.o_props[i].p_type == AMF_STRING)
        {
            pval = cobj.o_props[i].p_vu.p_aval;
            printf("\t\t %.*s: %.*s\n", pname.av_len, pname.av_val, pval.av_len, pval.av_val);
            if (AVMATCH(&pname, &av_app))
            {
                psession->app = pval;
            }
        }
    }
    RTMP_SendServerBW(psession->client_rtmp);
    RTMP_SendClientBW(psession->client_rtmp);
    RTMP_SendChunkSize(psession->client_rtmp, SERVER_CLIENT_CHUNK_SIZE);
    RTMP_SendResult(psession->client_rtmp, 1); // second parameter is called transaction ID, it is always 1 for connect
    RTMP_SendOnBWDone(psession->client_rtmp);
}

int method_releaseStream_handler(RTMP_PROXY_SESSION *psession, AMFObject obj)
{
    AVal stream_name;
    int transaction_id;

    AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &stream_name);
    transaction_id = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 1));

    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, transaction_id);
    *enc++ = AMF_NULL;
    *enc++ = AMF_UNDEFINED;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(psession->client_rtmp, &packet, FALSE);
}

int method_FCPublish_handler(RTMP_PROXY_SESSION *psession, AMFObject obj)
{
    AVal stream_name;
    int transaction_id;

    AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &stream_name);
    transaction_id = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 1));

    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, transaction_id);
    *enc++ = AMF_NULL;
    *enc++ = AMF_UNDEFINED;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(psession->client_rtmp, &packet, FALSE);
}

int method_createStream_handler(RTMP_PROXY_SESSION *psession, AMFObject obj)
{
    int transaction_id;
    transaction_id = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 1));

    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;

    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, transaction_id);
    *enc++ = AMF_NULL;
    enc = AMF_EncodeNumber(enc, pend, 1);

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(psession->client_rtmp, &packet, FALSE);
}

int method_publish_handler(RTMP_PROXY_SESSION *psession, AMFObject obj, int stream_id)
{
    char publish_url[256] = {};
    AVal resp_code_prop_val;
    AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &(psession->stream));

    rtmp_server_connect(psession, MODE_PUBLISH, psession->client_rtmp->m_inChunkSize);
    if (psession->server_rtmp == NULL || !RTMP_IsConnected(psession->server_rtmp))
    {
        return -1;
    }

    RTMP_SendOnFCPublish(psession->client_rtmp, stream_id);
    resp_code_prop_val.av_val = "NetStream.Publish.Start";
    resp_code_prop_val.av_len = strlen(resp_code_prop_val.av_val);
    RTMP_SendOnStatus(psession->client_rtmp, &resp_code_prop_val, stream_id);

    return 0;
}

int method_FCUnpublish_handler(RTMP_PROXY_SESSION *psession)
{
    rtmp_proxy_close(psession);
    return 0;
}

int method_play_handler(RTMP_PROXY_SESSION *psession, AMFObject obj, int stream_id)
{
    AVal resp_code_prop_val;

    psession->work_mode = MODE_PLAY;
    AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &(psession->stream));
    rtmp_server_connect(psession, MODE_PLAY, 0);
    if (psession->server_rtmp == NULL || !RTMP_IsConnected(psession->server_rtmp))
    {
        return -1;
    }

    RTMP_SendChunkSize(psession->client_rtmp, psession->server_rtmp->m_inChunkSize);

    AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &(psession->stream));
    RTMP_SendPlayStreamBegin(psession->client_rtmp);

    resp_code_prop_val.av_val = "NetStream.Play.Reset";
    resp_code_prop_val.av_len = strlen(resp_code_prop_val.av_val);
    RTMP_SendOnStatus(psession->client_rtmp, &resp_code_prop_val, stream_id);

    resp_code_prop_val.av_val = "NetStream.Play.Start";
    resp_code_prop_val.av_len = strlen(resp_code_prop_val.av_val);
    RTMP_SendOnStatus(psession->client_rtmp, &resp_code_prop_val, stream_id);

    RTMP_SendSampleAccess(psession->client_rtmp, stream_id);

    resp_code_prop_val.av_val = "NetStream.Data.Start";
    resp_code_prop_val.av_len = strlen(resp_code_prop_val.av_val);

    RTMP_SendOnStatus(psession->client_rtmp, &resp_code_prop_val, stream_id);

    return 0;
}

void rtmp_proxy_change_chunk_size(RTMP *r, const RTMPPacket *packet)
{
    if (packet->m_nBodySize >= 4)
    {
        r->m_inChunkSize = AMF_DecodeInt32(packet->m_body);
    }
}

int proxy_session_init(RTMP_PROXY_SESSION *session)
{
    session->work_mode = MODE_PUBLISH; // default is pubnlish

    session->client_rtmp = NULL;
    session->server_rtmp = NULL;

    session->next = NULL;
    session->prev = NULL;

    session->work_pt = 0;
    session->client_socket = 0;
}

int proxy_session_destroy(RTMP_PROXY_SESSION *psession)
{
    if (psession->client_rtmp != NULL)
    {
        RTMP_Free(psession->client_rtmp);
    }

    if (psession->server_rtmp != NULL)
    {
        RTMP_Free(psession->server_rtmp);
    }
}

int proxy_session_admin_init(RTMP_PROXY_SESSION_ADMIN *padmin)
{
    padmin->session_num = 0;
    padmin->start = NULL;
}

int proxy_session_admin_add(RTMP_PROXY_SESSION_ADMIN *padmin, RTMP_PROXY_SESSION *psession)
{
    RTMP_PROXY_SESSION *curr = NULL;
    if (padmin->start == NULL)
    {
        padmin->start = psession;
        psession->prev = NULL;
        padmin->session_num++;
        return 0;
    }
    curr = padmin->start;
    while (!curr->next)
    {
        curr = curr->next;
    }
    curr->next = psession;
    psession->prev = curr;
    padmin->session_num++;
}

int proxy_session_admin_remove(RTMP_PROXY_SESSION_ADMIN *padmin, RTMP_PROXY_SESSION *psession)
{
    RTMP_PROXY_SESSION *curr = NULL;
    if (padmin == NULL)
    {
        return 0;
    }

    if (padmin->start == psession)
    {
        if (psession->next == NULL)
        {
            padmin->start = NULL;
            padmin->session_num--;
        }
        else
        {
            padmin->start = psession->next;
            padmin->start->prev = NULL;
            padmin->session_num--;
        }
        return 0;
    }

    curr = padmin->start;
    while (curr != NULL)
    {
        if (curr != psession)
        {
            curr = curr->next;
        }
        else
        {
            curr->prev->next = curr->next;
            if (curr->next != NULL)
            {
                curr->next->prev = curr->prev;
            }
            padmin->session_num--;
        }
    }
    return 0;
}

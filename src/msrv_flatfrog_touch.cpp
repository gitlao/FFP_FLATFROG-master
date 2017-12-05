#include "../inc/msrv_flatfrog_touch.h"
#include "drvUART.h"

#if 1
#define DEFAULT_DEVICE "/dev/ttyS1"
#define DEFAULT_BAUD_RATE 921600
#else
#define DEFAULT_DEVICE "/dev/ttyS1"
#define DEFAULT_BAUD_RATE 115200
#endif

#if 1
/*android log*/
#include <android/log.h>
#define LOG_TAGS  "flatfrog"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAGS,__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,LOG_TAGS,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAGS,__VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAGS,__VA_ARGS__)
#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

// ruandelu 20171110 bgn

#define DBG_UART_PRT   1

#if DBG_UART_PRT

#define HHT_LOG_DEBUG(msg...)\
    do{\
        { \
            printf(" %s:%s:%d;",__FILE__,__func__,__LINE__);\
            printf(msg);\
        } \
    }while(0)

#else
    #define HHT_LOG_DEBUG(fmt, ...) 
#endif
// ruandelu 20171110 end
	
	
MSrv_flatfrog_touch*MSrv_flatfrog_touch::m_pInstance=NULL;
static int mask_zone_control_count =0;//区域触摸控制数目
//------------------------------------RATP defines---------------------------------
ratpUserHandle  T1;
/* Memory area used by RATP */
uint32_t        ratpinstance[RATP_INSTANCE_SIZE/4];
int             baud_rate, ratp_tick_rate;
/* Simple counter to send ffp_query with some interval */
int             counter ;
RATP_Return     m_ratp_return;
uint8_t         g_ratp_rvbuf[4096]={0};
//---------------------------------------------------------------------------------
struct ffuarthid_listener_options
{

    char device[256];
    int loop;
    unsigned int speed;
    int hdlc;
};
static int    g_serial_fd =-1;//全局串口描述符
static struct termios oldt, newt;

struct ffprotocol_device_options m_options;
int             server_socketfd;	//unix domain socket 描述符
int             client_socketfds[MAX_CONNECTION_CLIENTS];//客户端连接数组
static MASK_ZONE_CONTROL 	   m_mask_zone_control[MAX_DISABLE_MASK_ZONES];//触摸控制
static REQUEST_MASK_ZONE_INFO  m_request_mask_zone_info;//触摸区域
static int      uinp_fd = -1; //虚拟输入设备
//----------------------------------------------------------------------------------
static bool   isRunning;

pthread_t     m_thread_ratp,
	 m_pthread_domain_service,
	 m_pthread_connect_android;

uint8_t  m_two_flatbar_coordinate[16]={0};//两个flatbar坐标
static  bool  m_isoperation_on =false;//是否开启windows 停靠栏可触摸

MSrv_flatfrog_touch::MSrv_flatfrog_touch()
{
	isRunning =false;
	this->m_request_type=-1;
	this->m_request_data="";
	memset(&m_pthread_domain_service,0, sizeof(pthread_t));
	memset(&m_pthread_connect_android,0, sizeof(pthread_t));
	memset(&m_thread_ratp,0, sizeof(pthread_t));
}

MSrv_flatfrog_touch::~MSrv_flatfrog_touch()
{
	if(g_serial_fd >0)
	 close_serial_port(g_serial_fd);
}

MSrv_flatfrog_touch *MSrv_flatfrog_touch::GetInstance()
{
    if(m_pInstance ==NULL)
    {
		m_pInstance = new (std::nothrow) MSrv_flatfrog_touch; 
        assert(m_pInstance);
    }
    return m_pInstance;
}

void MSrv_flatfrog_touch::DestoryInstance()
{
    if(m_pInstance !=NULL)
    {
        delete m_pInstance;
        m_pInstance =NULL;
    }
}
/**
 * @brief create_unix_domain_socket_communication
 * 创建一个unix_domian_socket通讯服务
 */
int MSrv_flatfrog_touch::create_unix_domain_socket_conn_service()
{
    server_socketfd = -1;
    memset(client_socketfds,0,sizeof(client_socketfds));
#if 0
    unlink(UNIX_DOMAIN_SOCKET_NAME);
    server_socketfd= socket(AF_UNIX,SOCK_STREAM,0);
#else
    //init.rc 里面定义申请一个socket
    server_socketfd = android_get_control_socket(UNIX_DOMAIN_SOCKET_NAME);
#endif
    if(server_socketfd<=0)
    {
        //perror("socket() failed ...");
        printf("====>socket() failed ...\n");
        return -1;
    }
    LOGD("======>server_socketfd[%d]\n",server_socketfd);
    struct sockaddr_un local;
    //size_t server_len;
    memset(&local,0,sizeof(struct sockaddr_un));
    local.sun_family = AF_UNIX;
    //snprintf(local.sun_path,sizeof(local.sun_path),"%s",UNIX_DOMAIN_SOCKET_NAME);
    //server_len = sizeof(local);
    strcpy(local.sun_path,UNIX_DOMAIN_SOCKET_NAME);
    //server_len = strlen(local.sun_path)+sizeof(local.sun_family);
#if 0
    if(bind(server_socketfd,(struct sockaddr*)&local,server_len)<0)
    {
        perror("bind() failed ...");
        printf("====>bind() failed ...\n");
        //return -2;
    }
    if(chmod(SOCKET_PATH,0666)<0)
    {
        perror("chmod() failed ...");
        printf("====>chmod() failed ...\n");
        //return -3;
    }
#endif

    //LOGD("Listen() on port:%s\n",SOCKET_PATH);
    LOGD("Listen() on port:%s\n",UNIX_DOMAIN_SOCKET_NAME);
    printf("Listen() on port:%s\n",UNIX_DOMAIN_SOCKET_NAME);
    if(listen(server_socketfd,MAX_CONNECTION_CLIENTS)<0)
    {
        perror("listen() failed ...");
        printf("====>listen() failed ...\n");
        return -4;
    }
    socklen_t socketlen =sizeof(local);
    //fd_set readfds;
    //struct timeval timeout;
    //timeout.tv_sec =3;
    //timeout.tv_usec =0;
    LOGD("Waiting for connections...\n");
    printf("Waiting for connections...\n");
    //
    while (1)
    {
#if 0
        //clear the socket set
        FD_ZERO(&readfds);
        LOGD("======>server_socketfd[%d]\n",server_socketfd);
        //add server socet to set
        FD_SET(server_socketfd,&readfds);

        int max_fd = server_socketfd;
        int i =0;
		
        for(i;i<MAX_CONNECTION_CLIENTS;i++)
        {
            int fd = client_socketfds[i];
            if(fd >0)
            {
                FD_SET(fd,&readfds);
            }
            if(fd > max_fd)
            {
                max_fd = fd;
            }
        }
		
        //查询可用的socket描述符
        int activity_client = select(max_fd+1,&readfds,NULL,NULL,&timeout);
        if(activity_client <0 &&(errno !=EINTR))
        {
            perror("select() error...");
            HHT_LOG_DEBUG("select() error...\n");
            //break;
            continue;
        }
        LOGD("------------selecting()....-----------\n");
        //If something happened on the server socket , then its an incoming connection
        HHT_LOG_DEBUG("activty_client:[%d]...\n",activity_client);
        if(activity_client==0)
        {
            perror("slect() timout ...\n");
            LOGD("select() timeout ...\n");
            continue;
        }
        else
#endif
        {
            //if(FD_ISSET(server_socketfd,&readfds)) // disable
            {
                int new_client_fd =0;
                new_client_fd = accept(server_socketfd,(struct sockaddr *)&local, (socklen_t*)&socketlen);
                if(new_client_fd <0)
                {
                    //perror("accpet() error...\n");
                    LOGD("====>acpet() failed ...\n");
                    HHT_LOG_DEBUG("====>acpet() failed ...\n");
                    //exit(EXIT_FAILURE);
                    //return -5;
                    continue;
                }
                //
                HHT_LOG_DEBUG("---------------------------acpetting()---------------------------------\n");
                int i =0;
                for(i=0;i<MAX_CONNECTION_CLIENTS;i++)
                {
                    //if position is eimpty
                    if(client_socketfds[i]==0)
                    {
                        client_socketfds[i]=new_client_fd;
                        LOGD("adding client[%d] to the list of sockets...\n",new_client_fd);
                        HHT_LOG_DEBUG("adding client[%d] to the list of sockets...\n",new_client_fd);
                        pthread_t pthread;
                        if(pthread_create(&pthread,NULL,client_conn_handler,(void*) &client_socketfds[i])<0)
                        {
                            //perror("pthread_create() failed...");
                            LOGD("====>pthread_create client_conn_handler() failed ...\n");
                            HHT_LOG_DEBUG("====>pthread_create client_conn_handler() failed ...\n");
                            //return -6;
                        }
                        pthread_detach(pthread);
                        break;
                    }
                }
            }
        }
        sleep_ms(200);
        //睡眠一下降低CPU
    }

    return 0;
}

void *MSrv_flatfrog_touch::client_conn_handler(void *arg)
{
	MSrv_flatfrog_touch *msrv = (MSrv_flatfrog_touch*)arg;
	int client_socket  =msrv->client_socket; 
    //int client_socket = *(int*)arg;
    int read_size;
    char rvbuf[1024];
    memset(rvbuf,0,sizeof(rvbuf));
    while ((read_size=recv(client_socket,rvbuf,sizeof(rvbuf),0))>0)
    {
        //接收到客户端数据处理
        LOGD("------------------------------------------------------------------\n");
        LOGD("recv()client[%d]  msg[%s] size[%d]\n",client_socket,rvbuf,read_size);
        LOGD("------------------------------------------------------------------\n");
        HHT_LOG_DEBUG("----------------------------------------------------------------------\n");
        HHT_LOG_DEBUG("<<< recv()client[%d]  msg[%s] size[%d]\n",client_socket,rvbuf,read_size);
        HHT_LOG_DEBUG("----------------------------------------------------------------------\n");
        //开始处理接收的坐标如 (0,10,20,300,500)/(10,20,300,500)
        HHT_LOG_DEBUG("======================Waiting for handle the data=====================\n");
#if 0
        int request_type = msrv->get_request_type(rvbuf);
        HHT_LOG_DEBUG("<<< recv() client[%d] request type[%d]\n",client_socket,request_type);
		LOGD("<<< recv() client[%d] request type[%d]\n",client_socket,request_type);
        switch(request_type)
        {
        case 1:
        {
            //TODO enable all usb touch MaskZone->1
            msrv->enable_all_usb_touch_cmd();
            break;
        }
        case 2:
        {
            //TODO disable all usb touch MaskZOne->2
            msrv->disable_all_usb_touch_cmd();
            break;
        }
        case 3:
        {
            //TODO request new mask zone MaskZone->3
            //MaskZone->3|0,0,0,0 请求
            REQUEST_MASK_ZONE_INFO info = msrv->get_request_mask_zone_info(&rvbuf[12]);
            HHT_LOG_DEBUG("MASK ZONE>>> x0_low:[0x%02x] x0_high:[0x%02x] y0_low:[0x%02x] y0_high:[0x%02x]\nx1_low:[0x%02x] x1_high:[0x%02x] y1_low:[0x%02x] y1_high:[0x%02x]\n",
                 info.top_left_x_low,info.top_left_x_high,info.top_left_y_low,info.top_left_y_high,info.bottom_right_x_low,info.bottom_right_x_high,
                 info.bottom_right_y_low,info.bottom_right_y_high);

            int returnID = msrv->request_disable_usb_mask_zone(info);
            char sndbuf[8]={" "};
            if(returnID ==0)
            {
                memcpy(sndbuf,"0",4);//0
            }
            else if(returnID==1)
            {
                memcpy(sndbuf,"1",4);//1
            }
            else if(returnID==2)
            {
                memcpy(sndbuf,"2",4);//2
            }
            else
            {
                memcpy(sndbuf,"E",4);//error
            }
            //snprintf(sndbuf,sizeof(sndbuf),"id:%d",returnID);
            // int ret = send(client_socket,sndbuf,strlen(sndbuf), 0);//向客户端发送返回值
            int ret =  write(client_socket,sndbuf,strlen(sndbuf));
            HHT_LOG_DEBUG("=======>snd setid[%d] to clinet[%d]:status[%d] \n",atoi(sndbuf),client_socket,ret);
            if(ret>0)
            {
                HHT_LOG_DEBUG("=======>send() success.\n");
            }
            else
            {
                HHT_LOG_DEBUG("=======>send() error[%d]\n",errno);
            }
            memset(sndbuf,-1,sizeof(sndbuf));
            HHT_LOG_DEBUG("**********************************************************************\n");
            break;
        }
        case 4:
        {
            //TODO change id mask zone  MaskZone->4
            //MaskZone->4|id,0,0,0,0 更改
            REQUEST_MASK_ZONE_INFO info = msrv->get_request_mask_zone_info(&rvbuf[14]);
            HHT_LOG_DEBUG("MASK ZONE>>> x0_low:[0x%02x] x0_high:[0x%02x] y0_low:[0x%02x] y0_high:[0x%02x]\nx1_low:[0x%02x] x1_high:[0x%02x] y1_low:[0x%02x] y1_high:[0x%02x]\n",
                 info.top_left_x_low,info.top_left_x_high,info.top_left_y_low,info.top_left_y_high,info.bottom_right_x_low,info.bottom_right_x_high,
                 info.bottom_right_y_low,info.bottom_right_y_high);

            char dest[4];
            snprintf(dest,2,"%s",&rvbuf[12]);
            int ID = atoi(dest);//获取ID
            msrv->request_change_disable_usb_mask_zone(ID,info);
            break;
        }
        case 5:
        {
            //TODO release id mask zone MaskZone->5
            //MaskZone->5|id
            int controlID = atoi(&rvbuf[12]);
            msrv->release_usb_mask_zone_control(controlID); //释放对应ID的区域触摸
            break;
        }
        default:
        {
            char refuse[]="The server refused to provide the service";
            write(client_socket,refuse,strlen(refuse));
            break;
        }
        }
        memset(rvbuf,0,sizeof(rvbuf));
#else
        int request_type = msrv->get_static_request_type(rvbuf);
        HHT_LOG_DEBUG("========>request_type:[%d]\n",request_type);
        switch(request_type)
        {
        case 1:
        {
            //SetMask->id|0,0,0,0....
            #if 0
            char dest[4];
            snprintf(dest,2,"%s",&rvbuf[(int)strlen(SET_MASK_REQUEST)]);
            LOGD("dest[%s]\n",dest);
            int id =atoi(dest);
            HHT_LOG_DEBUG("1.=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id,&rvbuf[(int)strlen(SET_MASK_REQUEST)+2]);
            //set_mask_zone_with_id(id,&rvbuf[11]);
			#else
			string rvstring = rvbuf;
			int id =atoi(rvstring.substr((int)strlen(SET_MASK_REQUEST),1).data());
			LOGD("=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id,rvstring.substr((int)strlen(SET_MASK_REQUEST)+2).data());
			HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id,rvstring.substr((int)strlen(SET_MASK_REQUEST)+2).data());
			msrv->set_mask_zone_with_id(id,(char*)rvstring.substr((int)strlen(SET_MASK_REQUEST)+2).data());
			#endif
        }
        case 2:
        {
            //ChangeMask->id|0,0,0,0|0,0,0,0  (ChangeMask->id|0,0,0,0)
            #if 0
            char dest[4];
            snprintf(dest,2,"%s",&rvbuf[(int)strlen(CHANGE_MASK_REQUEST)]);
            LOGD("dest[%s]\n",dest);
            int id =atoi(dest);
            HHT_LOG_DEBUG("2.=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id,&rvbuf[(int)strlen(CHANGE_MASK_REQUEST)+2]);
            //set_mask_zone_with_id(id,&rvbuf[14]);
			#else
			string rvstring2 = rvbuf;
			int id2 =atoi(rvstring2.substr((int)strlen(CHANGE_MASK_REQUEST),1).data());
			LOGD("=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id2,rvstring2.substr((int)strlen(CHANGE_MASK_REQUEST)+2).data());
			HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id2,rvstring2.substr((int)strlen(CHANGE_MASK_REQUEST)+2).data());
			msrv->set_mask_zone_with_id(id2,(char*)rvstring2.substr((int)strlen(CHANGE_MASK_REQUEST)+2).data());
			#endif
        }
        case 3:
        {
            //ReleaseMask->id|0,0,0,0|0,0,0,0 (ReleaseMask->1|0,0,0,0)
            #if 0
            char dest[4];
            snprintf(dest,2,"%s",&rvbuf[(int)strlen(RELEASE_MASK_REQUEST)]);
            LOGD("dest[%s]\n",dest);
            int id =atoi(dest);
            HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id,&rvbuf[(int)strlen(RELEASE_MASK_REQUEST)+2]);
            //set_mask_zone_with_id(id,&rvbuf[15]);
			#else
			string rvstring3 = rvbuf;
			int id3 =atoi(rvstring3.substr((int)strlen(RELEASE_MASK_REQUEST),1).data());
			LOGD("3.=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id3,rvstring3.substr((int)strlen(RELEASE_MASK_REQUEST)+2).data());
			HHT_LOG_DEBUG("3.=======> type[%d],id[%d],rvbuf[%s]\n",request_type,id3,rvstring3.substr((int)strlen(RELEASE_MASK_REQUEST)+2).data());
			msrv->set_mask_zone_with_id(id3,(char*)rvstring3.substr((int)strlen(RELEASE_MASK_REQUEST)+2).data());
			#endif
        }
        default:
        	HHT_LOG_DEBUG("=======>lawless request...\n");
			LOGD("=======>lawless request...\n");
            break;
        }
        memset(rvbuf,0,sizeof(rvbuf));
#endif
    }
    if(read_size ==0)
    {
        LOGD("recv() client[%d] disconnected...\n",client_socket);
        HHT_LOG_DEBUG("<<< recv() client[%d] null ...\n",client_socket);
        //处理客户端异常，reset先前所有的屏蔽结果
    }
    else if (read_size ==-1)
    {
        //perror("recv() failed...\n");
        HHT_LOG_DEBUG("<<< recv() failed client[%d] disconnected...\n",client_socket);
        //mask_zone_control_count =0;
    }

    return (void*)0;
}

bool MSrv_flatfrog_touch::SendCallback(void *i_pUserHandle, const uint8_t *pu8Buf, uint16_t u16BufLen)
{
    ratpUserHandle *pHandle = (ratpUserHandle *)i_pUserHandle;
	UNUSED(pHandle);
    serial_write((unsigned char *)pu8Buf, u16BufLen);
    return true;
}

void MSrv_flatfrog_touch::RecvCallback(void *i_pUserHandle, uint8_t *pu8Buf, uint16_t u16BufLen)
{
    ratpUserHandle *pHandle = (ratpUserHandle *)i_pUserHandle;

    pHandle->RecvCallBackDataSize = u16BufLen;
    if (u16BufLen <= sizeof(pHandle->RecvCallBackData))
    {
        memcpy(pHandle->RecvCallBackData, pu8Buf, u16BufLen);
        pHandle->RecvCallBackDataSize = u16BufLen;
    }
}

void MSrv_flatfrog_touch::EvtCallback(void *i_pUserHandle, RATP_Return o_Type)
{
    string eventNames[] ={"Error", "Success", "Pending", "Opening", "Opened", "Closing", "Closed", "Disconnected", "Invalid"};
    ratpUserHandle *pHandle = (ratpUserHandle *)i_pUserHandle;
    LOGD("RATP Event: %s\n\n", eventNames[o_Type].data());
    HHT_LOG_DEBUG("RATP Event: %s\n\n", eventNames[o_Type].data());
#if  1
    if(o_Type ==RATP_OPENED)
    {
        ffprotocol_static_query();//查询握手确保连接
        HHT_LOG_DEBUG("====>connecting.....\n");
		LOGD("====>connecting.....\n");
    }
#endif
    pHandle->lastEvent = o_Type;

    /* Try to reconnect if connection breaks */
    if(RATP_status(ratpinstance) != RATP_OPENED)
    {
        LOGD("Reconnecting RATP...\n");
        HHT_LOG_DEBUG("Reconnecting RATP...\n");
        //RATP_close(ratpinstance);//关闭重新打开
        RATP_open(ratpinstance, &T1, ratp_tick_rate, baud_rate, SendCallback, RecvCallback, EvtCallback);
    }
}

int MSrv_flatfrog_touch::get_ratp_tick_rate(unsigned int baud_rate)
{
    switch(baud_rate)
    {
        case 9600: return 750;
        case 19200: return 400;
        case 38400: return 200;
        case 57600: return 120;
        case 115200: return 6;  // Use 6 ms tickrate for now
        case 230400: return 35;
        case 460800: return 15;
        case 921600: return 6;
        default: return 6;
    }
}

void MSrv_flatfrog_touch::ratp_tick_handler(clock_t *before)
{
    static int msec = 0;

    /* Calculate number of milliseconds since last RATP_tick */
    clock_t difference = clock() - *before;
    msec = difference * 1000 / CLOCKS_PER_SEC;

    if(msec >= ratp_tick_rate)
    {
        /* Every ratp_tick_rate milliseconds counter is incremented */
        counter++;
        /* RATP tick handles retransmissions and timeouts */
        RATP_tick(ratpinstance);
        *before = clock();
    }
}

unsigned int MSrv_flatfrog_touch::ratp_check_incoming(uint8_t *buf, unsigned int len)
{
    int nbr_received_bytes = 0;
    nbr_received_bytes = serial_read (buf, len);
    T1.RecvCallBackDataSize = 0;
    if(nbr_received_bytes > 0)
    {
        RATP_recv(ratpinstance, buf, nbr_received_bytes);
        memcpy(buf, T1.RecvCallBackData, T1.RecvCallBackDataSize);
    }

    return T1.RecvCallBackDataSize;
}

void MSrv_flatfrog_touch::handle_incoming_data(uint8_t *buf, unsigned int len)
{
#ifndef HHT_DEBUG
    LOGD("incoming data: %s\n",convert_hex_to_str(buf,len));
    HHT_LOG_DEBUG("incoming data: %s\n",convert_hex_to_str(buf,len));
#endif
	
    if(buf[0]==FFP_FTOUCH_EVENT)//接收ftouch event 数据 0x44
    {
        //5 点手指触摸事件
        //44 28 00  01 06 03 01 DE 12 3F 21   01 06 04 01 00 12 A4 1F
        //          01 06 05 01 1A 0F CE 1E 01 06 06 01 F5 12 A2 23
        //          01 06 07 01 C4 0A 82 21
        // i = i+(int)buf[i+1]+2 规律
        int event_data_len = (int)((buf[1])+(buf[2]<<8));
#ifndef HHT_DEBUG
        HHT_LOG_DEBUG("=====>Event data length: %d bytes\n",event_data_len);
#endif
        int i = 3 ;
        int j = 0 ;
		bool downflag=true;
		bool allupflag=true;
        while (1) {
            if(i+(int)buf[i+1]+2>=event_data_len+3)
            {
                unsigned int len = (unsigned int)buf[i+1]+2;
                uint8_t interaction_data[len];
                memcpy(interaction_data,&buf[i],len);
#ifndef HHT_DEBUG
                HHT_LOG_DEBUG("[POINT->%d]=====>%s \n",j,convert_hex_to_str(interaction_data,len));//抬起
#endif
                get_report_info_ctl(interaction_data,&downflag);
				if(downflag==true)
				{
					allupflag=false;
				}
                break;
            }
            else {
                unsigned int len = (unsigned int)buf[i+1]+2;
                uint8_t interaction_data[len];
                memcpy(interaction_data,&buf[i],len);
#ifndef HHT_DEBUG
                HHT_LOG_DEBUG("[POINT->%d]=====>%s \n",j,convert_hex_to_str(interaction_data,len));
#endif
                get_report_info_ctl(interaction_data,&downflag);
				if(downflag==true)
				{
					allupflag=false;
				}
                i = i+(int)buf[i+1]+2;
                j++;
            }
        }
        // SYN_REPORT
        if(allupflag==true)
        {
			//send_mt_abs_touch_key_up_event((int)0xffffffff,0,0);//丢弃
			send_mt_abs_touch_figner_up_event((int)0xffffffff,0,0);
			//send_mt_abs_touch_pen_up_event((int)0xffffffff,0,0);//丢弃
			send_mt_abs_stylus_up_event((int)0xffffffff,0,0);
        }
        struct input_event event;
        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        report_sync_event(uinp_fd, SYN_REPORT, &event.time);

		//判断睡眠状态，是睡眠状态，点击就唤醒
        #if 1
        int result = wakeup_system_when_sleep();
        if(result >0)
        {
            HHT_LOG_DEBUG("FTOUCH.=====>wakeup system status[%d]\n",result);
        }
		#endif
    }
	if((buf[0]==FFP_STATUS_DATA)&&(buf[1]==FFP_STATUS_OK)&&(buf[2]==FFP_FTOUCH_SENSOR_SIZE_GET))
	{
		LOGD("======>ffp_ftouch_sensor_size_get : %s\n",convert_hex_to_str(buf,len));
    	HHT_LOG_DEBUG("======>ffp_ftouch_sensor_size_get: %s\n",convert_hex_to_str(buf,len));
	}
    if((buf[0]==FFP_STATUS_DATA)&&(buf[1]==FFP_STATUS_OK)&&(buf[2]==FFP_CTRL_PROTOCOL_QUERY))
    {
        struct ffp_status_data *sd_reply = (struct ffp_status_data*) buf;
        LOGD("Got FFProtocol Query response: \n");
        HHT_LOG_DEBUG("Got FFProtocol Query response: \n");
        struct ffp_data_query *data_query = (struct ffp_data_query *) sd_reply->data;
        LOGD("API level: %x%x.%x%x\n", data_query->api_hi1, data_query->api_hi2, data_query->api_lo1, data_query->api_lo2);
        LOGD("\n");
        HHT_LOG_DEBUG("API level: %x%x.%x%x\n", data_query->api_hi1, data_query->api_hi2, data_query->api_lo1, data_query->api_lo2);
        HHT_LOG_DEBUG("\n");
        // ffprotocol_ftouch_uart_disable();//禁止FTOUCH UART 使能BTOUCH
        ffprotocol_ftouch_uart_enable();
    }
    if (buf[0]==FFP_STATUS&&(buf[1]==FFP_STATUS_OK)&&(buf[2]==FFP_FTOUCH_ENABLE))
    {
        LOGD("Sending Enable FTOUCH succsss !!!\n");
        HHT_LOG_DEBUG("Sending Enable FTOUCH succsss !!!\n");
		# if 1
			ffpprotocol_ftouch_sensor_size_get();//获取触摸框传感器SIZE
		#endif
        int result = wakeup_system_when_sleep();
        if(result >0)
        {
            HHT_LOG_DEBUG("1.=====>wakeup system status[%d]\n",result);
        }
    }
    if (buf[0]==FFP_STATUS&&(buf[1]==FFP_STATUS_OK)&&(buf[2]==FFP_FTOUCH_DISABLE))
    {
        HHT_LOG_DEBUG("Sending Disable FTOUCH succsss !!!\n");
        ffprotocol_btouch_uart_enable();//禁止ftouch uart后立马使能 btouch_uart
    }
    if(buf[0]==FFP_STATUS&&(buf[1]==FFP_STATUS_OK)&&(buf[2]==FFP_BTOUCH_ENABLE))
    {
        HHT_LOG_DEBUG("Sending Enable BTOUCH succsss !!!\n");
        int result = wakeup_system_when_sleep();
        if(result >0)
        {
            HHT_LOG_DEBUG("2.=====>wakeup system status[%d]\n",result);
        }
    }
    if(buf[2]==FFP_TOUCH_MASK_ZONES)
    {
        HHT_LOG_DEBUG("====>incoming mask zone return data:[%s]\n",convert_hex_to_str(buf,len));
		LOGD("====>incoming mask zone return data:[%s]\n",convert_hex_to_str(buf,len));
    }
    if((buf[2]==FFP_HTOUCH_DISABLE)||(buf[2]==FFP_HTOUCH_ENABLE))
    {
        HHT_LOG_DEBUG("====>incoming mask zone return data:[%s]\n",convert_hex_to_str(buf,len));
    }
   
}

void MSrv_flatfrog_touch::ffprotocol_static_query()
{
		HHT_LOG_DEBUG("static Sending FF Protocol Query\n\n");
		LOGD("static Sending FF Protocol Query\n\n");
#if 0
		uint8_t  protocol_query[49] = {
			0xE0, 0x2D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 	\
			0x00, 0x00, 0x00, 0x00, 'F', 'F', 'F', 'F','F', 	\
			'F', 'F', 'F','F','F', 'F','L', 'A', 'T', 'F',		\
			'R','O', 'G', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		\
			0, 0, 0, 0, 0, 0, 0, 0
		};
#else
		uint8_t  protocol_query[49] = {
			0xE0, 0x2D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 	\
			0x00, 0x00, 0x00, 0x00, 'F', 'F', 'F', 'F','F', 	\
			'F', 'F', 'F','F','F', 'F','L', 'A', 'T', 'F',		\
			'R','O', 'G', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		\
			0, 0, 0, 0, 0, 0, 0, 0
		};
	
#endif
		RATP_Return ret =RATP_send(ratpinstance, protocol_query, 49);
		string eventNames[] ={"Error", "Success", "Pending", "Opening", "Opened", "Closing", "Closed", "Disconnected", "Invalid"};
		HHT_LOG_DEBUG("RATP_send status :[%s]\n",eventNames[ret].data());
		LOGD("RATP_send status :[%s]\n",eventNames[ret].data()); 


}

void MSrv_flatfrog_touch::ffprotocol_query()
{
    HHT_LOG_DEBUG("Sending FF Protocol Query\n\n");
	LOGD("Sending FF Protocol Query\n\n");
#if 0
    uint8_t  protocol_query[49] = {
        0xE0, 0x2D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,     \
        0x00, 0x00, 0x00, 0x00, 'F', 'F', 'F', 'F','F',     \
        'F', 'F', 'F','F','F', 'F','L', 'A', 'T', 'F',      \
        'R','O', 'G', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,      \
        0, 0, 0, 0, 0, 0, 0, 0
    };
#else
    uint8_t  protocol_query[49] = {
        0xE0, 0x2D, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,     \
        0x00, 0x00, 0x00, 0x00, 'F', 'F', 'F', 'F','F',     \
        'F', 'F', 'F','F','F', 'F','L', 'A', 'T', 'F',      \
        'R','O', 'G', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,      \
        0, 0, 0, 0, 0, 0, 0, 0
    };

#endif
	RATP_Return ret =RATP_send(ratpinstance, protocol_query, 49);
	string eventNames[] ={"Error", "Success", "Pending", "Opening", "Opened", "Closing", "Closed", "Disconnected", "Invalid"};
 	HHT_LOG_DEBUG("RATP_send status :[%s]\n",eventNames[ret].data());
 	LOGD("RATP_send status :[%s]\n",eventNames[ret].data()); 
	
}

void MSrv_flatfrog_touch::ffprotocol_ftouch_uart_enable()
{
    LOGD("Sending FF Protocol Enable Uart\n\n");
    uint8_t protocol_ftouch_uart_enable[2]={
        0x40,0x02
    };
     RATP_send(ratpinstance,protocol_ftouch_uart_enable,sizeof(protocol_ftouch_uart_enable));
}

void MSrv_flatfrog_touch::ffprotocol_ftouch_uart_disable()
{
    LOGD("Sending FF Protocol Disable Uart\n\n");
    uint8_t protocol_ftouch_disable[2]={
        0x41,0x02
    };
     RATP_send(ratpinstance,protocol_ftouch_disable,sizeof(protocol_ftouch_disable));
}

void MSrv_flatfrog_touch::ffprotocol_btouch_uart_enable()
{
    uint8_t protocol_btouch_uart_enable[2]={
        0x20,0x02
    };
     RATP_send(ratpinstance,protocol_btouch_uart_enable,sizeof(protocol_btouch_uart_enable));
}

void MSrv_flatfrog_touch::ffprotocol_btouch_uart_disable()
{
    uint8_t protocol_btouch_uart_disable[2]={
        0x21,0x02
    };
     RATP_send(ratpinstance,protocol_btouch_uart_disable,sizeof(protocol_btouch_uart_disable));
}

void MSrv_flatfrog_touch::ffprotocol_btouch_usb_enable()
{
    uint8_t protocol_btouch_usb_enable[2]={
        0x20,0x01
    };
     RATP_send(ratpinstance,protocol_btouch_usb_enable,sizeof(protocol_btouch_usb_enable));
}

void MSrv_flatfrog_touch::ffprotocol_btouch_usb_disable()
{
    uint8_t protocol_btouch_usb_disable[2]={
        0x21,0x01
    };
     RATP_send(ratpinstance,protocol_btouch_usb_disable,sizeof(protocol_btouch_usb_disable));
}
void MSrv_flatfrog_touch::ffpprotocol_ftouch_sensor_size_get()
{
	uint8_t ffprotocol_sensor_size[1]={
        0x42
    };
     RATP_send(ratpinstance,ffprotocol_sensor_size,sizeof(ffprotocol_sensor_size));
}

void MSrv_flatfrog_touch::disable_all_usb_touch_cmd()
{
    HHT_LOG_DEBUG("disable_all_usb_touch_commd\n");
    uint8_t cmds [2]={0x11,0x03};
     RATP_send(ratpinstance,cmds,sizeof(cmds));
}

void MSrv_flatfrog_touch::enable_all_usb_touch_cmd()
{
    HHT_LOG_DEBUG("enable_all_usb_touch_commd\n");
    uint8_t cmds [2]={0x10,0x03};
     RATP_send(ratpinstance,cmds,sizeof(cmds));
}

Report_touch_information *MSrv_flatfrog_touch::get_touch_interaction_info(uint8_t *pBuf)
{
    Report_touch_information   *report_info;
    report_info =(struct _REPORT_TOUCH_INFORMATION*)malloc(sizeof(struct _REPORT_TOUCH_INFORMATION));
    memset(report_info,0,sizeof(struct _REPORT_TOUCH_INFORMATION));

    ffp_ftouch_event_interaction_touch *info = (struct _FFP_FTOUCH_EVENT_INTERACTION_TOUCH*)pBuf;
    unsigned int X = (unsigned int)((unsigned int)info->x_low+(unsigned int)(info->x_high<<8));
    unsigned int Y = (unsigned int)((unsigned int)info->y_low+(unsigned int)(info->y_high<<8));
#ifndef HHT_DEBUG
    HHT_LOG_DEBUG("**************TOUCH INTERACTION INFO**************\n");
     HHT_LOG_DEBUG("[interaction_type] : 0x%x\n",info->interaction_type);
     HHT_LOG_DEBUG("[interaction_data_len] : %d\n",(int)info->interaction_data_len);
     HHT_LOG_DEBUG("[Contact Identifier] : 0x%x\n",info->contact_id);
     HHT_LOG_DEBUG("[Modifiers] : 0x%x\n",info->modifiers);

    HHT_LOG_DEBUG("[X_low] :0x%x\n",info->x_low);
    HHT_LOG_DEBUG("[X_high] :0x%x\n",info->x_high);
    HHT_LOG_DEBUG("[Y_low] :0x%x\n",info->y_low);
    HHT_LOG_DEBUG("[Y_high] :0x%x\n",info->y_high);
    HHT_LOG_DEBUG("[X cooedinate] : %d\n",X);
    HHT_LOG_DEBUG("[Y coordintate] : %d\n\n",Y);
#endif
    report_info->interaction_type = (INTERACTION_TYPE)info->interaction_type;
    report_info->contact_id = info->contact_id;
    report_info->modifiers = (INTERACTION_STATUS)info->modifiers;
    report_info->interaction_x = X;
    report_info->interaction_y = Y;

    return report_info;
}

Report_touch_information *MSrv_flatfrog_touch::get_passive_pen_interaction_info(uint8_t *pBuf)
{
    Report_touch_information   *report_info;
    report_info =(struct _REPORT_TOUCH_INFORMATION*)malloc(sizeof(struct _REPORT_TOUCH_INFORMATION));
    memset(report_info,0,sizeof(struct _REPORT_TOUCH_INFORMATION));

    ffp_ftouch_event_interaction_passive_pen *info= (struct _FFP_FTOUCH_EVENT_INTERACTION_PASSIVE_PEN*)pBuf;
    unsigned int X = (unsigned int)((unsigned int)info->x_low+(unsigned int)(info->x_high<<8));
    unsigned int Y = (unsigned int)((unsigned int)info->y_low+(unsigned int)(info->y_high<<8));
#ifndef HHT_DEBUG
    HHT_LOG_DEBUG("**************PASSIVE PEN INTERACTION INFO**************\n");
    HHT_LOG_DEBUG("[interaction_type] : 0x%x\n",info->interaction_type);
    HHT_LOG_DEBUG("[interaction_data_len] : %d\n",(int)info->interaction_data_len);
    HHT_LOG_DEBUG("[Contact Identifier] : 0x%x\n",info->contact_id);
    HHT_LOG_DEBUG("[Pen type] : 0x%x\n",info->pen_type);
    HHT_LOG_DEBUG("[Modifiers] : 0x%x\n",info->modifiers);
    HHT_LOG_DEBUG("[X cooedinate] : %d\n",X);
    HHT_LOG_DEBUG("[Y coordintate] : %d\n\n",Y);
#endif

    report_info->interaction_type = (INTERACTION_TYPE)info->interaction_type;
    report_info->contact_id = info->contact_id;
    report_info->modifiers = (INTERACTION_STATUS)info->modifiers;
    report_info->interaction_x = X;
    report_info->interaction_y = Y;

    return report_info;
}

Report_touch_information *MSrv_flatfrog_touch::get_large_object_interaction_info(uint8_t *pBuf)
{
    Report_touch_information   *report_info;
    report_info =(struct _REPORT_TOUCH_INFORMATION*)malloc(sizeof(struct _REPORT_TOUCH_INFORMATION));
    memset(report_info,0,sizeof(struct _REPORT_TOUCH_INFORMATION));

    ffp_ftouch_event_interaction_large_object *info = (struct _FFP_FTOUCH_EVENT_INTERACTION_LARGE_OBJECT*)pBuf;

    unsigned int X= (unsigned int)((unsigned int)info->x_low+(unsigned int)(info->x_high<<8));
    unsigned int Y = (unsigned int)((unsigned int)info->y_low+(unsigned int)(info->y_high<<8));
#ifndef HHT_DEBUG
    HHT_LOG_DEBUG("**************LARGE OBJECT INTERACTION INFO**************\n");
    HHT_LOG_DEBUG("[interaction_type] : 0x%x\n",info->interaction_type);
    HHT_LOG_DEBUG("[interaction_data_len] : %d\n",(int)info->interaction_data_len);
    HHT_LOG_DEBUG("[Contact Identifier] : 0x%x\n",info->contact_id);
    HHT_LOG_DEBUG("[Modifiers] : 0x%x\n",info->modifiers);

    HHT_LOG_DEBUG("[X cooedinate] : %d\n",X);
    HHT_LOG_DEBUG("[Y coordintate] : %d\n",Y);
    HHT_LOG_DEBUG("[Major axis (mm)] : 0x%x\n",info->major);
    HHT_LOG_DEBUG("[Minor axis (mm)] : 0x%x\n",info->minor);
    HHT_LOG_DEBUG("[Azimuth axis (mm)] : 0x%x\n\n",info->azimuth);
#endif

    report_info->interaction_type = (INTERACTION_TYPE)info->interaction_type;
    report_info->contact_id = info->contact_id;
    report_info->modifiers = (INTERACTION_STATUS)info->modifiers;
    report_info->interaction_x = X;
    report_info->interaction_y = Y;

    return report_info;
}

Report_touch_information *MSrv_flatfrog_touch::get_active_pen_interaction_info(uint8_t *pBuf)
{
    Report_touch_information   *report_info;
    report_info =(struct _REPORT_TOUCH_INFORMATION*)malloc(sizeof(struct _REPORT_TOUCH_INFORMATION));
    memset(report_info,0,sizeof(struct _REPORT_TOUCH_INFORMATION));

    ffp_ftouch_event_interaction_active_pen *info = (struct _FFP_FTOUCH_EVENT_INTERACTION_ACTIVE_PEN*)pBuf;
    unsigned int X = (unsigned int)((unsigned int)info->x_low+(unsigned int)(info->x_high<<8));
    unsigned int Y = (unsigned int)((unsigned int)info->y_low+(unsigned int)(info->y_high<<8));
#ifndef HHT_DEBUG
    HHT_LOG_DEBUG("**************ACTIVE PEN INTERACTION INFO**************\n");
    HHT_LOG_DEBUG("[interaction_type] : 0x%x\n",info->interaction_type);
    HHT_LOG_DEBUG("[interaction_data_len] : %d\n",(int)info->interaction_data_len);
    HHT_LOG_DEBUG("[Serial number] : %x %x %x %x\n",info->serial_hi1,
           info->serial_hi2,info->serial_lo1,info->serial_lo2);
    HHT_LOG_DEBUG("Modifiers : 0x%x\n",info->modifiers);
    HHT_LOG_DEBUG("[X cooedinate] : %d\n",X);
    HHT_LOG_DEBUG("[Y coordintate] : %d\n",Y);
    HHT_LOG_DEBUG("[Pressure low] : 0x%x\n",info->pressure_low);
    HHT_LOG_DEBUG("[Pressure high] : 0x%x\n",info->pressure_high);
    HHT_LOG_DEBUG("[Pressure sensor] : %d\n",(unsigned int)((unsigned int)info->pressure_low+
                                                     (unsigned int)info->pressure_high<<8));
    HHT_LOG_DEBUG("Event data: %s\n\n",convert_hex_to_str(&info->event_data,64));
#endif
    report_info->interaction_type = (INTERACTION_TYPE)info->interaction_type;
    report_info->modifiers = (INTERACTION_STATUS)info->modifiers;
    report_info->interaction_x = X;
    report_info->interaction_y = Y;

    return report_info;
}

Report_touch_information *MSrv_flatfrog_touch::get_soft_key_interaction_info(uint8_t *pBuf)
{
    Report_touch_information   *report_info;
    report_info =(struct _REPORT_TOUCH_INFORMATION*)malloc(sizeof(struct _REPORT_TOUCH_INFORMATION));
    memset(report_info,0,sizeof(struct _REPORT_TOUCH_INFORMATION));
    ffp_ftouch_event__interaction_soft_key *info = (struct _FFP_FTOUCH_EVENT_INTERACTION_SOFT_KEY*)pBuf;
	UNUSED(info);
    HHT_LOG_DEBUG("**************SOFT KEY INTERACTION INFO**************\n");

    return report_info;
}
//incoming data: 0x44 0x09 0x00 0x02 0x07 0x1B 0x7E 0x01 0xDD 0x08 0xE0 0x30
void MSrv_flatfrog_touch::get_report_info_ctl(uint8_t *pBuf,bool* downflag)
{
    if(pBuf !=NULL)
    {
        switch (pBuf[0])
        {
        case 0x01:	//touch figner
        {
            Report_touch_information *info=get_touch_interaction_info(pBuf);
            if(info->modifiers==STATUS_DOWN)
            {
				//HHT_LOG_DEBUG("TOUCH EVENT_STATUS_DOWN,id=%d\n",(int)info->contact_id);
				//LOGD("TOUCH EVENT_STATUS_DOWN,id=%d\n",(int)info->contact_id);
                send_mt_abs_touch_figner_down_event((int)info->contact_id,(int)info->interaction_x,(int)info->interaction_y);
                *downflag=true;
            }
            else if(info->modifiers==STATUS_UP)
            {
				//HHT_LOG_DEBUG("TOUCH EVENT_STATUS_UP,id=%d\n",(int)info->contact_id);
				//LOGD("TOUCH EVENT_STATUS_UP,id=%d\n",(int)info->contact_id);
				*downflag=false;
            }

			//free
			if(info !=NULL)
			{
				free(info);
				info =NULL;
			}
        }
            break;
        case 0x02:  //pen stylus
        {
            Report_touch_information *info2=get_passive_pen_interaction_info(pBuf);
            if(info2->modifiers==STATUS_DOWN)
            {
            	//HHT_LOG_DEBUG("TOUCH PEN_STATUS_DOWN,id=%d\n",(int)info2->contact_id);
				//LOGD("TOUCH PEN_STATUS_DOWN,id=%d\n",(int)info2->contact_id);
				send_mt_abs_stylus_down_event((int)info2->contact_id,(int)info2->interaction_x,(int)info2->interaction_y);
                *downflag=true;
            }
            else if(info2->modifiers==STATUS_UP)
            {
            	//HHT_LOG_DEBUG("TOUCH PEN_STATUS_UP,id=%d\n",(int)info2->contact_id);
				//LOGD("TOUCH PEN_STATUS_UP,id=%d\n",(int)info2->contact_id);
				*downflag=false;
            }
			//free
			if(info2 !=NULL)
			{
				free(info2);
				info2 =NULL;
			}
        }
            break;
        case 0x03:	//large objects
        {
            Report_touch_information *info3=get_large_object_interaction_info(pBuf);
            if(info3->modifiers==STATUS_DOWN)
            {
            	//LOGD("RUBBER_STATUS_DOWN,id=%d\n",(int)info3->contact_id);
                send_mt_abs_touch_rubber_down_event((int)info3->contact_id,(int)info3->interaction_x,(int)info3->interaction_y);
                *downflag=true;
            }
            else if(info3->modifiers==STATUS_UP)
            {
            	//LOGD("RUBBER_STATUS_UP,id=%d\n",(int)info3->contact_id);
                send_mt_abs_touch_rubber_up_event((int)info3->contact_id,(int)info3->interaction_x,(int)info3->interaction_y);
				*downflag=false;
            }
			//free
			if(info3 !=NULL)
			{
				free(info3);
				info3 =NULL;
			}
        }
            break;
        case 0x04:	//active pen
        {
            Report_touch_information *info4=get_active_pen_interaction_info(pBuf);
			//HHT_LOG_DEBUG("RUBBER_STATUS_DOWN,id=%d\n",(int)info4->contact_id);
			UNUSED(info4);

        }
            break;
        case 0x05:
        {
        	//LOGD("======>SOFT KEY\n");
			//HHT_LOG_DEBUG("======>SOFT KEY\n");
            get_soft_key_interaction_info(pBuf);
        }
            break;
        default:
            break;
        }
    }
}

int MSrv_flatfrog_touch::set_usb_mask_zone_control_rect(int controlID, REQUEST_MASK_ZONE_INFO rect)
{
    if(mask_zone_control_count>MAX_DISABLE_MASK_ZONES)
    {
        HHT_LOG_DEBUG("1.=====>[set_usb_mask_zone_control_rect]|||REQUEST ERROR.\n");
        return -1;
    }
    if((controlID<0)&&(controlID>2))
    {
        HHT_LOG_DEBUG("2.=====>[set_usb_mask_zone_control_rect]|||REQUEST ERROR.\n");
        return -2;
    }
    m_mask_zone_control[controlID].isUsed = true;
    //TODO
    memcpy(&m_request_mask_zone_info,&rect,sizeof(rect));
    memcpy(&m_mask_zone_control[controlID].mRequset_mask_zone_info,&rect,sizeof(rect));

    return 0;
}

int MSrv_flatfrog_touch::request_disable_usb_mask_zone(REQUEST_MASK_ZONE_INFO rect)
{
    int i =0,j=0;
    HHT_LOG_DEBUG("=======>mask_zone_control_count:[%d]\n",mask_zone_control_count+1);
    // if(mask_zone_control_count>=MAX_DISABLE_MASK_ZONES)
    if(mask_zone_control_count>MAX_DISABLE_MASK_ZONES)
    {
        HHT_LOG_DEBUG("=======>[request_disable_usb_mask_zone]|||REQUEST ERROR.\n");
        return -1;
    }
    for(i=0;i<MAX_DISABLE_MASK_ZONES;i++)
    {
        if(!m_mask_zone_control[i].isUsed)
        {
            m_mask_zone_control[i].isUsed = true;
            break;
        }
    }
    set_usb_mask_zone_control_rect(i,rect);//
    // set_usb_mask_zone_control_rect(i+1,rect);//
    mask_zone_control_count++;
    //发送处理指令
    //TODO
    uint8_t cmdbuf[12];
    if(i==0)
    {
        memcpy(cmdbuf,"\x32\x00\x01\x01",4);
    }
    else if(i==1)
    {
        memcpy(cmdbuf,"\x32\x01\x01\x01",4);
    }
    else if(i==2)
    {
        memcpy(cmdbuf,"\x32\x02\x01\x01",4);
    }
    //sprintf(cmdbuf,"\x32\x%2d\x01\x01",i);
    memcpy(&cmdbuf[4],&m_request_mask_zone_info,sizeof(m_request_mask_zone_info));
    //写指令
    HHT_LOG_DEBUG(">>> send request_disable_usb_mask_zone :");
    for(j=0;j<12;j++)
    {
        HHT_LOG_DEBUG("0x%02x ",cmdbuf[j]);
    }
    HHT_LOG_DEBUG("\n");
     RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
     HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>request_disable_usb_mask_zone<<<<<<<<<<<<<<<\n");
    return (i);
    // return (i+1);
}

int MSrv_flatfrog_touch::release_usb_mask_zone_control(int controlID)
{
    if(mask_zone_control_count > MAX_DISABLE_MASK_ZONES)
    {
        HHT_LOG_DEBUG("1.=====>[release_usb_mask_zone_control]|||REQUEST ID[%d] ERROR.\n",controlID);
        return -1;
    }
    if(controlID<0)
    {
        HHT_LOG_DEBUG("2.=====>[release_usb_mask_zone_control]|||REQUEST ID[%d] ERROR.\n",controlID);
        return -2;
    }
    mask_zone_control_count--;
    m_mask_zone_control[controlID].isUsed = false;
    // m_mask_zone_control[controlID-1].isUsed = false;
    //设置对应ID的触摸区域数据
    //TODO
    memcpy(&m_request_mask_zone_info,&m_mask_zone_control[controlID].mRequset_mask_zone_info,sizeof(m_request_mask_zone_info));
    // memcpy(&m_request_mask_zone_info,&m_mask_zone_control[controlID-1].mRequset_mask_zone_info,sizeof(m_request_mask_zone_info));
    //uint8_t cmds [12]={0x32,id,0x01,0x00,/0x00,0x00,0x00,0x00,0x38,0x3b,0xa0,0x42};
    uint8_t cmdbuf[12];
    if(controlID==0)
    {
        memcpy(cmdbuf,"\x32\x00\x01\x00",4);
    }
    else if(controlID==1)
    {
        memcpy(cmdbuf,"\x32\x01\x01\x00",4);
    }
    else if(controlID==2)
    {
        memcpy(cmdbuf,"\x32\x02\x01\x00",4);
    }
    //sprintf(cmdbuf,"\x32\x%2d\x01\x00",controlID);
    memcpy(&cmdbuf[4],&m_request_mask_zone_info,sizeof(m_request_mask_zone_info));
    //写指令
    HHT_LOG_DEBUG("send release usb_maks zone control by id:[%d]\n",controlID);
    int i =0;
    for(i =0;i<12;i++)
    {
        HHT_LOG_DEBUG("0x%02d ",cmdbuf[i]);
    }
	
    HHT_LOG_DEBUG("\n");
    RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
    HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>release_usb_mask_zone_control<<<<<<<<<<<<<<<\n");
    return 0;
}

int MSrv_flatfrog_touch::request_change_disable_usb_mask_zone(int controlID, REQUEST_MASK_ZONE_INFO rect)
{
    if(controlID <=0||controlID >MAX_DISABLE_MASK_ZONES)
    {
        HHT_LOG_DEBUG("=======>[request_change_disable_usb_mask_zone] |||REQUEST CHANGE ID[%d] ERROR.\n",controlID);
        return -1;
    }
    release_usb_mask_zone_control(controlID);//取消先前触摸区域
    mask_zone_control_count++;
    set_usb_mask_zone_control_rect(controlID,rect);//设置现在修改区域坐标
    //设置现在修改区域
    //TO DO
    //uint8_t cmds [12]={0x32,id,0x01,0x00,/0x00,0x00,0x00,0x00,0x38,0x3b,0xa0,0x42};
    uint8_t cmdbuf[12];
    if(controlID==0)
    {
        memcpy(cmdbuf,"\x32\x00\x01\x00",4);
    }
    else if(controlID==1)
    {
        memcpy(cmdbuf,"\x32\x01\x01\x00",4);
    }
    else if(controlID==2)
    {
        memcpy(cmdbuf,"\x32\x02\x01\x00",4);
    }
    //sprintf(cmdbuf,"\x32\x%2d\x01\x00",controlID);
    memcpy(&cmdbuf[4],&m_request_mask_zone_info,sizeof(m_request_mask_zone_info));
    //写指令
    HHT_LOG_DEBUG("send request_change_disable_usb_mask_zone with id[%d]:",controlID);
	HHT_LOG_DEBUG("%s\n",convert_hex_to_str(cmdbuf,12));
    RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
     HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>request_change_disable_usb_mask_zone<<<<<<<<<<<<<<<\n");
    return controlID;
}

REQUEST_MASK_ZONE_INFO MSrv_flatfrog_touch::get_request_mask_zone_info(char *rvbuf)
{
    REQUEST_MASK_ZONE_INFO  info;
    memset(&info,0,sizeof(info));
    int xy[4]={0},i=0;
    char *pToken = strtok(rvbuf,",");
    while(pToken!=NULL)
    {
        xy[i++] =atoi(pToken);
        pToken = strtok(NULL,",");
    }
#if 0
    info.top_left_x_low = (uint8_t)((xy[0]*XRATIO)&0x00ff);
    info.top_left_x_high =(uint8_t)((xy[0]*XRATIO)>>8);

    info.top_left_y_low = (uint8_t)((xy[1]*YRATIO)&0x00ff);
    info.top_left_y_high =(uint8_t)((xy[1]*YRATIO)>>8);

    info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO)&0x00ff);
    info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO)>>8);

    info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO)&0x00ff);
    info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO)>>8);
#else//可靠区域获取
    info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
    info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

    info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
    info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

    info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
    info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

    info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
    info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
#endif
    return info;
}

int MSrv_flatfrog_touch::get_request_type(const char *rvbuf)
{
    if(rvbuf==NULL)
        return -1;
    if(strstr(rvbuf,MASK_ZONE_REQUEST))
        return 1;
    else if(strstr(rvbuf,MASK_ZONE_REQUEST2))
        return 2;
    else if(strstr(rvbuf,MASK_ZONE_REQUEST3))
        return 3;
    else if(strstr(rvbuf,MASK_ZONE_REQUEST4))
        return 4;
    else if(strstr(rvbuf,MASK_ZONE_REQUEST5))
        return 5;
    return 0;
}

void MSrv_flatfrog_touch::release_flatbar_mak_zone(int id,uint8_t *location)
{
	uint8_t cmdbuf[20];
    memset(cmdbuf,0,sizeof(cmdbuf));
	memcpy(cmdbuf,"\x32\x00\x02\x00",4);
	//memcpy(cmdbuf,"\x32\x00\x02\x01",4);
	if(location)
	{
		memcpy(&cmdbuf[4],location,16);
	}
	else
	{
		return ;
	}
	LOGD("release_flatbar_mak_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
    RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
    LOGD(">>>>>>>>>>>>>>>>release_flatbar_mak_zone()<<<<<<<<<<<<<<<\n");
}

void MSrv_flatfrog_touch::set_all_mask_zones_with_one_id(int id,char *coordinate)
{
    REQUEST_MASK_ZONE_INFO  info,info2;
    memset(&info,0,sizeof(info));
    memset(&info2,0,sizeof(info2));
    if(id==0)//表示两个flatbar坐标
    {
    	uint8_t cmdbuf[20];
    	memset(cmdbuf,0,sizeof(cmdbuf));
        int xy[4]={0},xy2[4]={0},i=0,j=0;
        char *strArray[2];
        split(strArray,coordinate,"|");//截断
        char *pToken = strtok(strArray[0],",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
#if 0
        int x =0;
        for(x;x<4;x++)
        {
            printf("xy[%d]:%d",x,xy[x]);
        }
#endif
        char *pToken2 = strtok(strArray[1],",");
        while(pToken2)
        {
            xy2[j++] =atoi(pToken2);
            pToken2 = strtok(NULL,",");
        }
#if 0
        int y =0;
        for(y;y<4;y++)
        {
            printf("xy2[%d]:%d",y,xy2[y]);
        }
#endif
		int result = i+j;
		if(result!=8)
		{
			HHT_LOG_DEBUG("--->id =0 数据不合法\n");
			LOGD("--->id =0 数据不合法\n");
			return;
		}
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        //----------------------------------------------------------
        info2.top_left_x_low = (uint8_t)((xy2[0]*XRATIO2)&0x00ff);
        info2.top_left_x_high =(uint8_t)((xy2[0]*XRATIO2)>>8);

        info2.top_left_y_low = (uint8_t)((xy2[1]*YRATIO2)&0x00ff);
        info2.top_left_y_high =(uint8_t)((xy2[1]*YRATIO2)>>8);

        info2.bottom_right_x_low = (uint8_t)((xy2[2]*XRATIO2)&0x00ff);
        info2.bottom_right_x_high =(uint8_t)((xy2[2]*XRATIO2)>>8);

        info2.bottom_right_y_low = (uint8_t)((xy2[3]*YRATIO2)&0x00ff);
        info2.bottom_right_y_high= (uint8_t)((xy2[3]*YRATIO2)>>8);

        memcpy(cmdbuf,"\x32\x00\x02\x01",4);
        memcpy(&cmdbuf[4],&info,sizeof(info));
        memcpy(&cmdbuf[12],&info2,sizeof(info2));

		memset(m_two_flatbar_coordinate, 0, sizeof(m_two_flatbar_coordinate));
		memcpy(m_two_flatbar_coordinate,&cmdbuf[4],sizeof(m_two_flatbar_coordinate));//获取flatbar坐标缓存
		LOGD("1--------------------------------------------------------------->: %s\n",convert_hex_to_str(m_two_flatbar_coordinate,16));
        //写指令
        LOGD(">>> send set_mask_zone id[%d]:\n",id);
		LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
        RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        LOGD(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
	else  ///传入参数必须一个点坐标
	{
		int xy[4]={0},i=0;//,j=0;
        char *pToken = strtok(coordinate,",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }

		if(i!=4)
		{
			HHT_LOG_DEBUG("--->id=1|id=2 数据不合法\n");
			LOGD("--->id=1|id=2 数据不合法\n");
			return;
		}
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
		switch(id)//1 2 3 4 5 ...
		{
			case 1:	//批注   spotil_button
				{
				if(!m_isoperation_on)//未开启
				{
					uint8_t cmdbuf[28];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x03\x00",4);//
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					memcpy(&cmdbuf[20],&info,sizeof(info));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,28));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
				}
				else{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					}
					break;
				}
			case 2:	//白板   whiteboard_button
				{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 3:	//操作    operation_on
				{
					m_isoperation_on =true;
					uint8_t cmdbuf[28];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x03\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					memcpy(&cmdbuf[20],&info,sizeof(info));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,28));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 4:	//操作    operation_off  
				{
					m_isoperation_on =false;
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 5:	//白板透明    whiteboard_show
				{
					if(m_isoperation_on)
					{
						uint8_t cmdbuf[28];
    					memset(cmdbuf,0,sizeof(cmdbuf));
						memcpy(cmdbuf,"\x32\x00\x03\x01",4);
						memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
						memcpy(&cmdbuf[20],&info,sizeof(info));
					
						LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,28));
						RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//						
					}
					else
					{
						uint8_t cmdbuf[20];
    					memset(cmdbuf,0,sizeof(cmdbuf));
						memcpy(cmdbuf,"\x32\x00\x02\x02",4);
						memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					
						LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
						RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					}
					break;
				}
			case 6:	//白板透明    whiteboard_hide
				{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 7:	//操作	operation_up
				{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));

					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 8:	//操作	operation_down
				{
					uint8_t cmdbuf[28];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x03\x01",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
					memcpy(&cmdbuf[20],&info,sizeof(info));
					
					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,28));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			case 9:	//quicksetting
				{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));

					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
			default:
				{
					uint8_t cmdbuf[20];
    				memset(cmdbuf,0,sizeof(cmdbuf));
					memcpy(cmdbuf,"\x32\x00\x02\x02",4);
					memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));

					LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
					RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
					break;
				}
		}
	}
	 LOGD(">>>>>>>>>>>>>>>>set_all_mask_zones_with_one_id()<<<<<<<<<<<<<<<\n");
     HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>set_all_mask_zones_with_one_id()<<<<<<<<<<<<<<<\n");
}

void MSrv_flatfrog_touch::set_mask_zone_with_id(int id, char *coordinate)
{
    uint8_t cmdbuf[20];
    memset(cmdbuf,0,sizeof(cmdbuf));
    REQUEST_MASK_ZONE_INFO  info,info2;
    memset(&info,0,sizeof(info));
    memset(&info2,0,sizeof(info2));
    if(id==0)
    {
        int xy[4]={0},xy2[4]={0},i=0,j=0;
        char *strArray[2];
        split(strArray,coordinate,"|");//截断
        char *pToken = strtok(strArray[0],",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
#if 1
        int x =0;
        for(x=0;x<4;x++)
        {
            LOGD("xy[%d]:%d",x,xy[x]);
        }
#endif
        char *pToken2 = strtok(strArray[1],",");
        while(pToken2)
        {
            xy2[j++] =atoi(pToken2);
            pToken2 = strtok(NULL,",");
        }
#if 1
        int y =0;
        for(y=0;y<4;y++)
        {
            LOGD("xy2[%d]:%d",y,xy2[y]);
        }
#endif
		int result = i+j;
		if(result!=8)
		{
			HHT_LOG_DEBUG("--->id =0 数据不合法\n");
			LOGD("--->id =0 数据不合法\n");
			return;
		}
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        //----------------------------------------------------------
        info2.top_left_x_low = (uint8_t)((xy2[0]*XRATIO2)&0x00ff);
        info2.top_left_x_high =(uint8_t)((xy2[0]*XRATIO2)>>8);

        info2.top_left_y_low = (uint8_t)((xy2[1]*YRATIO2)&0x00ff);
        info2.top_left_y_high =(uint8_t)((xy2[1]*YRATIO2)>>8);

        info2.bottom_right_x_low = (uint8_t)((xy2[2]*XRATIO2)&0x00ff);
        info2.bottom_right_x_high =(uint8_t)((xy2[2]*XRATIO2)>>8);

        info2.bottom_right_y_low = (uint8_t)((xy2[3]*YRATIO2)&0x00ff);
        info2.bottom_right_y_high= (uint8_t)((xy2[3]*YRATIO2)>>8);

        memcpy(cmdbuf,"\x32\x00\x02\x01",4);
        memcpy(&cmdbuf[4],&info,sizeof(info));
        memcpy(&cmdbuf[12],&info2,sizeof(info2));

		memset(m_two_flatbar_coordinate, 0, sizeof(m_two_flatbar_coordinate));
		memcpy(m_two_flatbar_coordinate,&cmdbuf[4],sizeof(m_two_flatbar_coordinate));//获取flatbar坐标缓存
		LOGD("1--------------------------------------->: %s\n",convert_hex_to_str(m_two_flatbar_coordinate,16));
        //写指令
        LOGD(">>> send set_mask_zone id[%d]:\n",id);
		LOGD("send set_mask_zone cmds: %s\n",convert_hex_to_str(cmdbuf,20));
        RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        LOGD(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
    else if((id==1)||(id==2))
    {
        int xy[4]={0},i=0;//,j=0;
        char *pToken = strtok(coordinate,",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
#if 1
		int y =0;
		for(y=0;y<4;y++)
		{
			LOGD("xy[%d]:%d",y,xy[y]);
		}
#endif

		if(i!=4)
		{
			HHT_LOG_DEBUG("--->id=1|id=2 数据不合法\n");
			LOGD("--->id=1|id=2 数据不合法\n");
			return;
		}
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        if(id==1)
        {
            memcpy(cmdbuf,"\x32\x01\x01\x01",4);
        }
        else if(id==2)
        {
            memcpy(cmdbuf,"\x32\x02\x01\x01",4);
        }
        memcpy(&cmdbuf[4],&info,sizeof(info));
        //写指令
		//release_flatbar_mak_zone(0, m_two_flatbar_coordinate);
		//sleep_ms(100);
		
        LOGD(">>>send set_mask_zone id[%d]:\n",id);
		LOGD("send set_mask_zone cmd: %s\n",convert_hex_to_str(cmdbuf,20));
		RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//

		//memcpy(&cmdbuf[4],m_two_flatbar_coordinate,sizeof(m_two_flatbar_coordinate));
		//LOGD("send set_mask_zone cmd: %s\n",convert_hex_to_str(cmdbuf,20));

        LOGD(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>set_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
}

void MSrv_flatfrog_touch::release_mask_zone_with_id(int id, char *coordinate)
{
    uint8_t cmdbuf[20];
    memset(cmdbuf,0,sizeof(cmdbuf));
    REQUEST_MASK_ZONE_INFO  info,info2;
    memset(&info,0,sizeof(info));
    memset(&info2,0,sizeof(info2));
    if(id==0)
    {
        int xy[4]={0},xy2[4]={0},i=0,j=0;
        char *strArray[2];
        split(strArray,coordinate,"|");//截断
        char *pToken = strtok(strArray[0],",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
#if 0
        int x =0;
        for(x;x<4;x++)
        {
            printf("xy[%d]:%d",x,xy[x]);
        }
#endif
        char *pToken2 = strtok(strArray[1],",");
        while(pToken2)
        {
            xy2[j++] =atoi(pToken2);
            pToken2 = strtok(NULL,",");
        }
#if 0
        int y =0;
        for(y;y<4;y++)
        {
            printf("xy2[%d]:%d",y,xy2[y]);
        }
#endif
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        //----------------------------------------------------------
        info2.top_left_x_low = (uint8_t)((xy2[0]*XRATIO2)&0x00ff);
        info2.top_left_x_high =(uint8_t)((xy2[0]*XRATIO2)>>8);

        info2.top_left_y_low = (uint8_t)((xy2[1]*YRATIO2)&0x00ff);
        info2.top_left_y_high =(uint8_t)((xy2[1]*YRATIO2)>>8);

        info2.bottom_right_x_low = (uint8_t)((xy2[2]*XRATIO2)&0x00ff);
        info2.bottom_right_x_high =(uint8_t)((xy2[2]*XRATIO2)>>8);

        info2.bottom_right_y_low = (uint8_t)((xy2[3]*YRATIO2)&0x00ff);
        info2.bottom_right_y_high= (uint8_t)((xy2[3]*YRATIO2)>>8);

        memcpy(cmdbuf,"\x32\x00\x02\x00",4);
        memcpy(&cmdbuf[4],&info,sizeof(info));
        memcpy(&cmdbuf[12],&info2,sizeof(info2));

        //写指令
        LOGD(">>> send release_mask_zone_with_id[%d]:\n",id);
		LOGD("send release_mask_zone cmd: %s\n",convert_hex_to_str(cmdbuf,20));
        RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>release_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
		LOGD(">>>>>>>>>>>>>>>>release_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
    else if((id==1)||(id==2))
    {
        int xy[4]={0},i=0;
        char *pToken = strtok(coordinate,",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        if(id==1)
        {
            memcpy(cmdbuf,"\x32\x01\x01\x00",4);
        }
        else if(id==2)
        {
            memcpy(cmdbuf,"\x32\x02\x01\x00",4);
        }
        memcpy(&cmdbuf[4],&info,sizeof(info));
        //写指令
        LOGD(">>> send release_mask_zone_with_id[%d]:\n",id);
		LOGD("send release_mask_zone cmd:  %s\n",convert_hex_to_str(cmdbuf,12));
        //RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        enable_all_usb_touch_cmd();
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>release_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
		LOGD(">>>>>>>>>>>>>>>>release_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
}

void MSrv_flatfrog_touch::change_mask_zone_with_id(int id, char *coordinate)
{
    uint8_t cmdbuf[20];
    memset(cmdbuf,0,sizeof(cmdbuf));
    REQUEST_MASK_ZONE_INFO  info,info2;
    memset(&info,0,sizeof(info));
    memset(&info2,0,sizeof(info2));
    if(id==0)
    {
        int xy[4]={0},xy2[4]={0},i=0,j=0;
        char *strArray[2];
        split(strArray,coordinate,"|");//截断
        char *pToken = strtok(strArray[0],",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
#if 0
        int x =0;
        for(x;x<4;x++)
        {
            printf("xy[%d]:%d",x,xy[x]);
        }
#endif
        char *pToken2 = strtok(strArray[1],",");
        while(pToken2)
        {
            xy2[j++] =atoi(pToken2);
            pToken2 = strtok(NULL,",");
        }
#if 0
        int y =0;
        for(y;y<4;y++)
        {
            printf("xy2[%d]:%d",y,xy2[y]);
        }
#endif
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        //----------------------------------------------------------
        info2.top_left_x_low = (uint8_t)((xy2[0]*XRATIO2)&0x00ff);
        info2.top_left_x_high =(uint8_t)((xy2[0]*XRATIO2)>>8);

        info2.top_left_y_low = (uint8_t)((xy2[1]*YRATIO2)&0x00ff);
        info2.top_left_y_high =(uint8_t)((xy2[1]*YRATIO2)>>8);

        info2.bottom_right_x_low = (uint8_t)((xy2[2]*XRATIO2)&0x00ff);
        info2.bottom_right_x_high =(uint8_t)((xy2[2]*XRATIO2)>>8);

        info2.bottom_right_y_low = (uint8_t)((xy2[3]*YRATIO2)&0x00ff);
        info2.bottom_right_y_high= (uint8_t)((xy2[3]*YRATIO2)>>8);

        memcpy(cmdbuf,"\x32\x00\x02\x01",4);
        memcpy(&cmdbuf[4],&info,sizeof(info));
        memcpy(&cmdbuf[12],&info2,sizeof(info2));

	    memset(m_two_flatbar_coordinate, 0, sizeof(m_two_flatbar_coordinate));
		memcpy(m_two_flatbar_coordinate,&cmdbuf[4],sizeof(m_two_flatbar_coordinate));//获取flatbar坐标缓存
		LOGD("3--------------------------------------->: %s\n",convert_hex_to_str(m_two_flatbar_coordinate,16));
        //写指令
        LOGD(">>> send change_mask_zone_with id[%d]:\n",id);
		LOGD("send change_mask_zone cmd: %s\n",convert_hex_to_str(cmdbuf,20));
        RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>change_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
		LOGD(">>>>>>>>>>>>>>>>change_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
    else if((id==1)||(id==2))
    {
        int xy[4]={0},i=0;//,j=0;
        char *pToken = strtok(coordinate,",");
        while(pToken!=NULL)
        {
            xy[i++] =atoi(pToken);
            pToken = strtok(NULL,",");
        }
        info.top_left_x_low = (uint8_t)((xy[0]*XRATIO2)&0x00ff);
        info.top_left_x_high =(uint8_t)((xy[0]*XRATIO2)>>8);

        info.top_left_y_low = (uint8_t)((xy[1]*YRATIO2)&0x00ff);
        info.top_left_y_high =(uint8_t)((xy[1]*YRATIO2)>>8);

        info.bottom_right_x_low = (uint8_t)((xy[2]*XRATIO2)&0x00ff);
        info.bottom_right_x_high =(uint8_t)((xy[2]*XRATIO2)>>8);

        info.bottom_right_y_low = (uint8_t)((xy[3]*YRATIO2)&0x00ff);
        info.bottom_right_y_high= (uint8_t)((xy[3]*YRATIO2)>>8);
        if(id==1)
        {
            memcpy(cmdbuf,"\x32\x01\x01\x01",4);
        }
        else if(id==2)
        {
            memcpy(cmdbuf,"\x32\x02\x01\x01",4);
        }
        memcpy(&cmdbuf[4],&info,sizeof(info));
        //写指令
        LOGD(">>> send change_mask_zone_with_id[%d]:\n",id);
		LOGD("send change_mask_zone cmd: %s\n",convert_hex_to_str(cmdbuf,12));
        //RATP_send(ratpinstance,cmdbuf,sizeof(cmdbuf));//
        enable_all_usb_touch_cmd();
        HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>change_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
		LOGD(">>>>>>>>>>>>>>>>change_mask_zone_with_id()<<<<<<<<<<<<<<<\n");
    }
}

int MSrv_flatfrog_touch::get_static_request_type(const char *rvbuf)
{
#if 0
    if(rvbuf==NULL)
        return -1;
    if(strstr(rvbuf,SET_MASK_REQUEST))
        return 1;
    else if(strstr(rvbuf,CHANGE_MASK_REQUEST))
        return 2;
    else if(strstr(rvbuf,RELEASE_MASK_REQUEST))
        return 3;
#else
	if(rvbuf==NULL)
        return -1;
    if(strncmp(rvbuf,SET_MASK_REQUEST,(int)strlen(SET_MASK_REQUEST))==0)
        return 1;
    else if(strncmp(rvbuf,CHANGE_MASK_REQUEST,(int)strlen(CHANGE_MASK_REQUEST))==0)
        return 2;
    else if(strncmp(rvbuf,RELEASE_MASK_REQUEST,(int)strlen(RELEASE_MASK_REQUEST))==0)
        return 3;
#endif
    return 0;
}

void MSrv_flatfrog_touch::clear_all_mask_zones()
{
    mask_zone_control_count =0;
    int i=0;
    for(i=0;i<MAX_DISABLE_MASK_ZONES;i++)
    {
        memset(&m_mask_zone_control[i],0,sizeof(m_mask_zone_control[i]));
        m_mask_zone_control[i].isUsed =false;
    }
    uint8_t cmds [2]={0x10,0x03};
     RATP_send(ratpinstance,cmds,sizeof(cmds));

    uint8_t setid_cmds[12]={0x32,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
     RATP_send(ratpinstance,setid_cmds,sizeof(setid_cmds));

    uint8_t setid2_cmds[12]={0x32,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
     RATP_send(ratpinstance,setid2_cmds,sizeof(setid2_cmds));

    uint8_t setid3_cmds[12]={0x32,0x02,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
     RATP_send(ratpinstance,setid3_cmds,sizeof(setid3_cmds));
     HHT_LOG_DEBUG(">>>>>>>>>>>>>>>>trying to clear all mask zone setting<<<<<<<<<<<<<<<\n");
}

int MSrv_flatfrog_touch::wakeup_system_when_sleep()
{
    //判断睡眠状态
    char status[2];
    size_t size;
    int file_fd=-1;
    file_fd = open("/sys/power/wake_lock",O_RDONLY);
    if(file_fd < 0)
    {
        HHT_LOG_DEBUG("open() /sys/power/wake_lock failed.\n");
        close(file_fd);
        return -1;
    }
    if(file_fd >0 )
    {
        size = read(file_fd,status,sizeof(status));
        //LOGD("====>read() system wake status[0x%2x]\n",status[0]);
        close(file_fd);//读取完立马结束
        if(size >0)
        {
            if(status[0]< 0x20)//为睡眠状态 0x01 (sleep) 0x50(display)
            {
                //发送睡眠按键
                LOGD("=====>system is in sleeping now...\n");
                HHT_LOG_DEBUG("====>read() system wake status[0x%2x]\n",status[0]);
                struct input_event event,event2;
                memset(&event, 0, sizeof(event));
                memset(&event2, 0, sizeof(event2));

                gettimeofday(&event.time, NULL);
                report_key_event(uinp_fd,KEY_POWER,1,&event.time);
                report_sync_event(uinp_fd,KEY_POWER,&event.time);
                //
                gettimeofday(&event2.time, NULL);
                report_key_event(uinp_fd,KEY_POWER,0,&event2.time);
                report_sync_event(uinp_fd,KEY_POWER,&event2.time);
                return 1;
            }
            //LOGD("====>system is on display...\n");
        }
    }
    return 0;
}

void MSrv_flatfrog_touch::split(char **arr, char *str, const char *delims)
{
    char *s = strtok(str, delims);
    while(s != NULL)
    {
        *arr++ = s;
        s = strtok(NULL, delims);
    }
}

char *MSrv_flatfrog_touch::convert_hex_to_str(unsigned char *pBuf, const int nLen)
{
    static char    acBuf[20000]    = {0,};
    char           acTmpBuf[10]    = {0,};
    int            ulIndex         = 0;
    int            ulBufLen        = 0;

    if ((NULL == pBuf) || (0 >= nLen))
    {
        return NULL;
    }

    ulBufLen = sizeof(acBuf)/sizeof(acTmpBuf);
    if (ulBufLen >= nLen)
    {
        ulBufLen = nLen;
    }

    memset(acBuf, 0, sizeof(acBuf));
    memset(acTmpBuf, 0, sizeof(acTmpBuf));

    for(ulIndex=0; ulIndex<ulBufLen; ulIndex++)
    {
        //snprintf(acTmpBuf, sizeof(acTmpBuf), "0x%02X ", *(pBuf + ulIndex));
        snprintf(acTmpBuf, sizeof(acTmpBuf), "%02X ", *(pBuf + ulIndex));
        strcat(acBuf, acTmpBuf);
    }
    return acBuf;
}

void MSrv_flatfrog_touch::sleep_ms(unsigned int msec)
{
    struct timeval tval;
    tval.tv_sec = msec/1000;
    tval.tv_usec = (msec*1000)%1000000;
    select(0,NULL,NULL,NULL,&tval);
}

int MSrv_flatfrog_touch::create_virtual_input_device()
{
    struct uinput_user_dev uinp;
    uinp_fd = open("/dev/uinput",O_WRONLY|O_NDELAY);
    if(uinp_fd <=0)
    {
        return -1;
    }

    memset(&uinp,0x00,sizeof(uinp));

#if  DEVICE_X9
    strncpy(uinp.name,"FFProtocol UART TP-X9-86",sizeof(uinp.name)-1);

    uinp.id.vendor = 0x3697;
    uinp.id.product=0x0003;
    uinp.id.bustype = BUS_VIRTUAL;

    uinp.absmax[ABS_X] =0x7671;
    uinp.absmin[ABS_X] = 0;
    uinp.absmax[ABS_Y] = 0x429f;
    uinp.absmin[ABS_Y] = 0;
    uinp.absmax[ABS_PRESSURE]= 15000;
    uinp.absmin[ABS_PRESSURE]= 0;

    uinp.absmax[ABS_MT_POSITION_X] = 0x7671;
    uinp.absmin[ABS_MT_POSITION_X] = 0;
    uinp.absmax[ABS_MT_POSITION_Y] = 0x429f;
    uinp.absmin[ABS_MT_POSITION_Y] = 0;
    uinp.absmax[ABS_MT_TRACKING_ID] = 0xFFFF;
    uinp.absmin[ABS_MT_TRACKING_ID] = 0;
#endif
#if  DEVICE_X8
	strncpy(uinp.name,"FFProtocol UART TP-X8-75",sizeof(uinp.name)-1);

    uinp.id.vendor = 0x3697;
    uinp.id.product=0x0003;
    uinp.id.bustype = BUS_VIRTUAL;

    uinp.absmax[ABS_X] =0x671f;
    uinp.absmin[ABS_X] = 0;
    uinp.absmax[ABS_Y] = 0x3a02;
    uinp.absmin[ABS_Y] = 0;
    uinp.absmax[ABS_PRESSURE]= 15000;
    uinp.absmin[ABS_PRESSURE]= 0;

    uinp.absmax[ABS_MT_POSITION_X] = 0x671f;
    uinp.absmin[ABS_MT_POSITION_X] = 0;
    uinp.absmax[ABS_MT_POSITION_Y] = 0x3a02;
    uinp.absmin[ABS_MT_POSITION_Y] = 0;
    uinp.absmax[ABS_MT_TRACKING_ID] = 0xFFFF;
    uinp.absmin[ABS_MT_TRACKING_ID] = 0;
#endif
#if  DEVICE_X6
	strncpy(uinp.name,"FFProtocol UART TP-X6-65",sizeof(uinp.name)-1);

    uinp.id.vendor = 0x3697;
    uinp.id.product=0x0003;
    uinp.id.bustype = BUS_VIRTUAL;

    uinp.absmax[ABS_X] =0x5948;
    uinp.absmin[ABS_X] = 0;
    uinp.absmax[ABS_Y] = 0x3238;
    uinp.absmin[ABS_Y] = 0;
    uinp.absmax[ABS_PRESSURE]= 15000;
    uinp.absmin[ABS_PRESSURE]= 0;

    uinp.absmax[ABS_MT_POSITION_X] = 0x5948;
    uinp.absmin[ABS_MT_POSITION_X] = 0;
    uinp.absmax[ABS_MT_POSITION_Y] = 0x3238;
    uinp.absmin[ABS_MT_POSITION_Y] = 0;
    uinp.absmax[ABS_MT_TRACKING_ID] = 0xFFFF;
    uinp.absmin[ABS_MT_TRACKING_ID] = 0;
#endif

    if(write(uinp_fd,&uinp,sizeof(uinp))<0)
    {
        close(uinp_fd);
        return -2;
    }
    if(ioctl(uinp_fd,UI_SET_EVBIT,EV_KEY/*0x01*/)!=0)
    {
        close(uinp_fd);
        return -3;
    }

    if(ioctl(uinp_fd, UI_SET_EVBIT, EV_ABS) != 0)
    {
        close(uinp_fd);
        return -4;
    }

    if(ioctl(uinp_fd, UI_SET_EVBIT, EV_REL) != 0)
    {
        close(uinp_fd);
        return -5;
    }

    if(ioctl(uinp_fd,UI_SET_KEYBIT, BTN_TOUCH) != 0)
    {
        close(uinp_fd);
        return -6;
    }

    if(ioctl(uinp_fd, UI_SET_KEYBIT, BTN_BACK) != 0)
    {
        close(uinp_fd);
        return -7;
    }

    if(ioctl(uinp_fd,UI_SET_KEYBIT, BTN_TOOL_PEN) != 0)//
    {
        close(uinp_fd);
        return -8;
    }

    if(ioctl(uinp_fd, UI_SET_KEYBIT, BTN_TOOL_FINGER) != 0)//
    {
        close(uinp_fd);
        return -8;
    }

    if(ioctl(uinp_fd, UI_SET_KEYBIT, BTN_TOOL_RUBBER) != 0)
    {
        close(uinp_fd);
        return -9;
    }

    if(ioctl(uinp_fd, UI_SET_KEYBIT, BTN_STYLUS) != 0)
    {
        close(uinp_fd);
        return -10;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_X) != 0)
    {
        close(uinp_fd);
        return -11;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_Y) != 0)
    {
        close(uinp_fd);
        return -12;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) != 0)
    {
        close(uinp_fd);
        return -13;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) != 0)
    {
        close(uinp_fd);
        return -14;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) != 0)
    {
        close(uinp_fd);
        return -15;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_MT_TOOL_TYPE) != 0)
    {
        close(uinp_fd);
        return -20;
    }

    /********************/

    if(ioctl(uinp_fd, UI_SET_RELBIT, REL_X) != 0)
    {
        close(uinp_fd);
        return -16;
    }

    if(ioctl(uinp_fd, UI_SET_RELBIT, REL_Y) != 0)
    {
        close(uinp_fd);
        return -17;
    }

    if(ioctl(uinp_fd, UI_SET_ABSBIT, ABS_PRESSURE) != 0)
    {
        close(uinp_fd);
        return -18;
    }
    int i;
    for(i=0; i<0x1FF; i++)
    {
        ioctl(uinp_fd, UI_SET_KEYBIT, i);
    }

    if (ioctl(uinp_fd, UI_DEV_CREATE))//注册设备
    {
        return -19;
    }

    return 0;
}

void MSrv_flatfrog_touch::send_mt_abs_touch_key_down_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
	
#if 1	//20171117修订
	report_key_event(uinp_fd, BTN_TOUCH, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 1, &event.time);
	report_abs_event(uinp_fd, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER, &event.time);
	
    report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
#endif
}

void MSrv_flatfrog_touch::send_mt_abs_touch_key_up_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
	
#if 1	//20171117修订
    report_key_event(uinp_fd, BTN_TOUCH, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);

    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 0, &event.time);

    //report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
 #endif
}

void MSrv_flatfrog_touch::send_mt_abs_touch_figner_down_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

#if 1	//20171117修订
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 1, &event.time);
	report_abs_event(uinp_fd, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER, &event.time);
	
	report_key_event(uinp_fd, BTN_TOUCH, 1, &event.time);
	report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
#endif

}

void MSrv_flatfrog_touch::send_mt_abs_touch_figner_up_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
	
#if 1 	//20171117修订
	report_key_event(uinp_fd, BTN_TOUCH, 0, &event.time);
    report_key_event(uinp_fd, BTN_TOOL_FINGER, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);

    //report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 0, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 0, &event.time);

    //report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
#endif
}

void MSrv_flatfrog_touch::send_mt_abs_touch_pen_down_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

#if 1	//20171117修订
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 1, &event.time);
    report_key_event(uinp_fd, BTN_TOOL_PEN, 1, &event.time);
	report_key_event(uinp_fd, BTN_TOUCH, 1, &event.time);
	report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
#endif

}

void MSrv_flatfrog_touch::send_mt_abs_touch_pen_up_event(int pos_id,int xpos,int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

#if 1  //20171117修订
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
	
    //report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 0, &event.time);
    //report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 0, &event.time);
	report_key_event(uinp_fd, BTN_TOUCH, 0, &event.time);
    report_key_event(uinp_fd, BTN_TOOL_PEN, 0, &event.time);
#endif

}

void MSrv_flatfrog_touch::send_mt_abs_stylus_down_event(int pos_id,int xpos,int ypos)//STYLUS
{
	struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
	report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);

    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 1, &event.time);
	report_abs_event(uinp_fd, ABS_MT_TOOL_TYPE, MT_TOOL_PEN, &event.time);
	report_key_event(uinp_fd, BTN_TOUCH, 1, &event.time);
	report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
}
void MSrv_flatfrog_touch::send_mt_abs_stylus_up_event(int pos_id,int xpos,int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

	report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
	report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 0, &event.time);
	
	report_key_event(uinp_fd, BTN_TOUCH, 0, &event.time);
    report_abs_event(uinp_fd,ABS_MT_TOOL_TYPE, 0, &event.time);
	report_abs_event(uinp_fd,MT_TOOL_PEN, 0, &event.time);
}

void MSrv_flatfrog_touch::send_mt_abs_touch_rubber_down_event(int pos_id, int xpos, int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

#if 1	//20171117修订
    report_key_event(uinp_fd, BTN_TOUCH, 1, &event.time);
    report_key_event(uinp_fd, BTN_STYLUS, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);

    report_key_event(uinp_fd, BTN_TOOL_RUBBER, 1, &event.time);
    report_key_event(uinp_fd, ABS_MISC, 1, &event.time);
    report_key_event(uinp_fd, MSC_SERIAL, 1, &event.time);

    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 1, &event.time);
#endif

}

void MSrv_flatfrog_touch::send_mt_abs_touch_rubber_up_event(int pos_id,int xpos,int ypos)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

#if 1	//20171117修订
    report_key_event(uinp_fd, BTN_TOUCH, 0, &event.time);
    report_key_event(uinp_fd, BTN_STYLUS, 0, &event.time);
    report_key_event(uinp_fd, BTN_TOOL_RUBBER, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, xpos, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, ypos, &event.time);
    report_key_event(uinp_fd, MSC_SERIAL, 0, &event.time);

    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 0, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MINOR, 0, &event.time);
#endif

}

void MSrv_flatfrog_touch::send_mt_abs_event(int pos_id, int abs_x, int abs_y)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

    report_abs_event(uinp_fd, ABS_MT_TRACKING_ID, pos_id, &event.time);
    report_abs_event(uinp_fd, ABS_MT_TOUCH_MAJOR, 1, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_X, abs_x, &event.time);
    report_abs_event(uinp_fd, ABS_MT_POSITION_Y, abs_y, &event.time);

	report_sync_event(uinp_fd, SYN_MT_REPORT, &event.time);
}

int MSrv_flatfrog_touch::report_key_event(int fd, unsigned short code, int pressed, timeval *time)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.time.tv_sec=time->tv_sec;
    event.time.tv_usec=time->tv_usec;
    event.type = EV_KEY;//EV_KEY;
    event.code = code;
    event.value = !!pressed;
    return (write(fd, &event, sizeof(event)));
}

int MSrv_flatfrog_touch::report_rel_event(int fd, unsigned short code, int value, timeval *time)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.time.tv_sec=time->tv_sec;
    event.time.tv_usec=time->tv_usec;
    event.type = EV_REL;
    event.code = code;
    event.value = value;
    return (write(fd, &event, sizeof(event)));
}

int MSrv_flatfrog_touch::report_abs_event(int fd, unsigned short code, int value, timeval *time)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.time.tv_sec=time->tv_sec;
    event.time.tv_usec=time->tv_usec;
    event.type = EV_ABS;
    event.code = code;
    event.value = value;
    return (write(fd, &event, sizeof(event)));
}

int MSrv_flatfrog_touch::report_sync_event(int fd, int code, timeval *time)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.time.tv_sec=time->tv_sec;
    event.time.tv_usec=time->tv_usec;
    event.type = EV_SYN;
    event.code = code;
    event.value = 0;
    return (write(fd, &event, sizeof(event)));
}

//创建unix domain socket 服务线程
void* MSrv_flatfrog_touch::create_domain_socket_service_pthread(void*arg)
{
	MSrv_flatfrog_touch* msrv = (MSrv_flatfrog_touch*)arg;
	
	int result = msrv->create_unix_domain_socket_conn_service();
	LOGD("=====>craete unix domain service status:[%d]\n",result);
	HHT_LOG_DEBUG("=====>craete_unix_domain_service_status:[%d]\n",result);
	return ((void*)0);
}

void MSrv_flatfrog_touch::set_receive_data(string rvbuf)
{
	this->m_request_data= rvbuf;
}
 string MSrv_flatfrog_touch::get_receive_data()
{
	return this->m_request_data;
}

 void MSrv_flatfrog_touch::set_receive_type(int type)
 {
	this->m_request_type=type;
 }
 int MSrv_flatfrog_touch::get_receive_type()
 {
 	return this->m_request_type;
 }

void* MSrv_flatfrog_touch::create_deal_data_from_android_pthread(void*arg)
{
	MSrv_flatfrog_touch *msrv = (MSrv_flatfrog_touch*)arg;
	while (1)
	{
		if(msrv->m_request_data !="")
		{
			//TODO  id|0,0,0,0|0,0,0,0
			HHT_LOG_DEBUG("rvbuf from android ->type:[%d] data:[%s]\n",msrv->m_request_type,msrv->m_request_data.data());
			LOGD("rvbuf from android ->type:[%d] data:[%s]\n",msrv->m_request_type,msrv->m_request_data.data());
			switch (msrv->m_request_type)
			{
				case 1:  //SetMask
				{
					int id =atoi(msrv->m_request_data.substr(0,1).data());
					LOGD("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					msrv->set_mask_zone_with_id(id,(char*)msrv->m_request_data.substr(2).data());
					break;
				}
				case 2:	//ChangeMask
				{
					int id =atoi(msrv->m_request_data.substr(0,1).data());
					LOGD("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					msrv->change_mask_zone_with_id(id,(char*)msrv->m_request_data.substr(2).data());
					break;
				}
				case 3:  //ReleaseMask
				{
					int id =atoi(msrv->m_request_data.substr(0,1).data());
					LOGD("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					HHT_LOG_DEBUG("=======> type[%d],id[%d],rvbuf[%s]\n",msrv->m_request_type,id,msrv->m_request_data.substr(2).data());
					msrv->release_mask_zone_with_id(id,(char*)msrv->m_request_data.substr(2).data());
					break;
				}
				case 4:  //SET_CHILDREN_LOCK  
				{
					LOGD("=======>Set Children Clock\n");
					HHT_LOG_DEBUG("=======>Set Children Clock\n");
					msrv->disable_all_usb_touch_cmd();  //关闭所有触摸
					msrv->sleep_ms(100);
					msrv->ffprotocol_btouch_uart_disable();
					break;
				}
				case 5:  //RELEASE_CHILDREN_LOCK
				{
					LOGD("=======>Release Children Clock\n");
					HHT_LOG_DEBUG("=======>Release Children Clock\n");
					msrv->enable_all_usb_touch_cmd();//打开所有触摸
					msrv->sleep_ms(100);
					msrv->ffprotocol_btouch_uart_enable();
					break;
				}
				case 6:  //ENABLE_ALL_USB_MASK_ZONE
				{
					LOGD("=======>ENABLE_ALL_USB_MASK_ZONE\n");
					//HHT_LOG_DEBUG("=======>ENABLE_ALL_USB_MASK_ZONE\n");
					msrv->enable_all_usb_touch_cmd();//打开所有USB触摸
					break;
				}
				case 7:  //DISABLE_ALL_USB_MASK_ZONE
				{
					LOGD("=======>DISABLE_ALL_USB_MASK_ZONE\n");
					//HHT_LOG_DEBUG("=======>DISABLE_ALL_USB_MASK_ZONE\n");
					msrv->disable_all_usb_touch_cmd();//关闭所有USB触摸
					break;
				}
				default:
				{
					msrv->m_request_type =-1;
					msrv->m_request_data="";
					break;
				}
			}
			msrv->m_request_type =-1;
			msrv->m_request_data="";
			
		}
	    msrv->sleep_ms(100);
	}
	return ((void*)0);
}

void* MSrv_flatfrog_touch::Run(void*arg)
{
	MSrv_flatfrog_touch *msrv = (MSrv_flatfrog_touch*)arg;
	//if(!isRunning)
	{
		//创建虚拟输入设备
		int ret = msrv->create_virtual_input_device();
		if(ret ==0)
		{
			HHT_LOG_DEBUG("[create_virtual_input_device() success.]\n");
			LOGD("[create_virtual_input_device() success.]\n");
		}
		else
		{
			HHT_LOG_DEBUG("[create_virtual_input_device() failed.]\n");
			LOGD("[create_virtual_input_device() failed.]\n");
		}
#if 0	
		//创建 unix domain service
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr,1024);
		int result= pthread_create(&m_pthread_domain_service,&attr,create_domain_socket_service_pthread,(void*)0);
		LOGD("=====>create_domain_socket_service_pthread_status:[%d]\n",result);
		HHT_LOG_DEBUG("=====>create_domain_socket_service_pthread_status:[%d]\n",result);
#else
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr,1024);
		int result= pthread_create(&m_pthread_connect_android,&attr,create_deal_data_from_android_pthread,msrv);
		LOGD("=====>create_deal_data_from_android_pthread_status:[%d]\n",result);
		HHT_LOG_DEBUG("=====>create_deal_data_from_android_pthread_status:[%d]\n",result);
#endif
		memset(&m_options,0,sizeof(struct ffprotocol_device_options));
    	m_options.speed=DEFAULT_BAUD_RATE;
    	char default_device[]=DEFAULT_DEVICE;
    	strncpy(m_options.device,default_device,sizeof(m_options.device));
    	char *portname = m_options.device;
    	HHT_LOG_DEBUG("Opening serial connection...\n");
    	LOGD("Opening serial connection...\n");
    	int result2 =msrv->open_serial_port(portname,m_options.speed);
    	if(result2 >0)
    	{
        	LOGD("Connected on serial port \"%s\" with baud rate \"%d\"\n", m_options.device, m_options.speed);
        	HHT_LOG_DEBUG("Connected on serial port \"%s\" with baud rate \"%d\"\n", m_options.device, m_options.speed);
    	}
    	else
    	{
        	LOGD("Connected on serial port \"%s\" failed\n", m_options.device);
        	HHT_LOG_DEBUG("Connected on serial port \"%s\" failed\n", m_options.device);
        	//return ((void*)0);
    	}

		msrv->sleep_ms(300);
		
    	LOGD("Opening RATP connection...\n");
    	HHT_LOG_DEBUG("Opening RATP connection...\n");

    	baud_rate = m_options.speed;
    	ratp_tick_rate =msrv->get_ratp_tick_rate(baud_rate);
    	m_ratp_return = RATP_open(ratpinstance,&T1,ratp_tick_rate,m_options.speed,SendCallback,RecvCallback,EvtCallback);
    	if(m_ratp_return==RATP_SUCCESS)
    	{
        	LOGD("Success to init RATP !!!\n");
        	HHT_LOG_DEBUG("Success to init RATP !!!\n");
    	}
    	else
    	{
        	LOGD("Failed to init RATP !!!\n");
        	HHT_LOG_DEBUG("Failed to init RATP !!!\n");
        	//return ((void*)0);
    	}
		msrv->sleep_ms(300);
		//-------------------------------------------------------------------
		//clock_t before=clock();
    	msrv->ffprotocol_query();//查询握手确保连接
    	LOGD("*******TRY TO KEEP CONNECT.********\n");
        HHT_LOG_DEBUG("*******TRY TO KEEP CONNECT.********\n");
        //bool  status =false;
    	 while (1)
    	 {
        	int buflen =0;
		#if 0	//屏蔽特定时钟握手一次
        	if(counter ==300)
        	{
            	if((!status)||(RATP_status(ratpinstance)==RATP_OPENED))
            	{
                	msrv->ffprotocol_query();//查询握手确保连接
                	status = true;
                	LOGD("*******TRY TO KEEP CONNECT.********\n");
                	HHT_LOG_DEBUG("*******TRY TO KEEP CONNECT.********\n");
            	}
            	counter =0;
        	}
			msrv->ratp_tick_handler(&before);
		#endif
        	buflen = msrv->ratp_check_incoming(g_ratp_rvbuf,sizeof(g_ratp_rvbuf));
        	if(buflen==0) 
        	{
            	;
        	}
        	else
        	{
            	msrv->handle_incoming_data(g_ratp_rvbuf,buflen);
        	}
			memset(g_ratp_rvbuf,0,sizeof(g_ratp_rvbuf));
			msrv->sleep_ms(1);
    	}

		isRunning =true;
	}
	return ((void*)0);
}


//供第三方启动服务
void MSrv_flatfrog_touch::start()
{

	pthread_attr_t thr_attr;		
	pthread_attr_init(&thr_attr);		 
	pthread_attr_setstacksize(&thr_attr, 1024);
#if 1
	int ret = pthread_create(&m_thread_ratp, &thr_attr,Run, this);
#else
	clock_t before=clock();
	this->before = before;
	int ret = pthread_create(&m_thread_ratp, &thr_attr,Run, this);
#endif
	if (ret)		
	{			 
		HHT_LOG_DEBUG("create Run thread failed\n");
		LOGD("createRun thread failed\n");
	}

}

//------------------------------------------------------------------------------------------
ssize_t MSrv_flatfrog_touch::serial_write(unsigned char *buf, unsigned int len)
{
    unsigned int writes = write (g_serial_fd, buf, len);
    if( writes != len) {
        HHT_LOG_DEBUG("%s\n", strerror( errno ));
    }
    return writes;
}

int MSrv_flatfrog_touch::serial_read(unsigned char *buf, unsigned int len)
{
    int buflen = read (g_serial_fd, buf, len);  	// read up to 100 characters if ready to read

    return buflen;
}

int MSrv_flatfrog_touch::open_serial_port(char *device, unsigned int speed)
{
    char *portname = device;

    g_serial_fd= open (portname, O_RDWR | O_NOCTTY | O_SYNC| O_NONBLOCK);
    if (g_serial_fd < 0)
    {
        HHT_LOG_DEBUG("error %d opening %s: %s\n", errno, portname, strerror (errno));
        return -1;
    }
#if 1	
	  MDrv_UART_SetIOMapBase(); 
	  MDrv_UART_Init(E_UART_PIU_UART1,speed);
	  mdrv_uart_connect(E_UART_PORT1, E_UART_PIU_UART1);
#else
	  MDrv_UART_SetIOMapBase(); 
	  MDrv_UART_Init(E_UART_PIU_UART1,speed);
	  mdrv_uart_connect(E_UART_PORT0, E_UART_PIU_UART1);
#endif

#if 1
    speed_t speed_code = baud_to_code(speed);
    //assert(speed_code != B0);
    if (set_interface_attribs (g_serial_fd, speed_code, 0))
    { // set speed and 8n1 (no parity)
        return -1;
    }
    set_blocking (g_serial_fd, 0);                // set no blocking

    /*tcgetattr gets the parameters of the current terminal
      STDIN_FILENO will tell tcgetattr that it should write the settings
      of stdin to oldt*/
    tcgetattr( STDIN_FILENO, &oldt);
    /*now the settings will be copied*/
    newt = oldt;

    /*ICANON normally takes care that one line at a time will be processed
      that means it will return if it sees a "\n" or an EOF or an EOL*/
    newt.c_lflag &= ~(ICANON);
    newt.c_cc[VMIN]  = 0; // non blocking
    newt.c_cc[VTIME] = 0;            // 0 seconds read timeout

    /*Those new settings will be set to STDIN
      TCSANOW tells tcsetattr to change attributes immediately. */
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);
    HHT_LOG_DEBUG("%s success...\n",__FUNCTION__);
#endif
    return g_serial_fd;
}


void MSrv_flatfrog_touch::close_serial_port(int fd)
{
    close(fd);
    fd = -1;//close device fd,reset the value
}

int MSrv_flatfrog_touch::set_interface_attribs(int fd, speed_t speed, int parity)
{
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0) {
        HHT_LOG_DEBUG ("error %d from tcgetattr", errno);
        return -1;
    }

    cfsetospeed (&tty, speed);
    cfsetispeed (&tty, speed);
    //assert(cfgetospeed (&tty) == speed);
    //assert(cfgetispeed (&tty) == speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_iflag &= ~IGNCR;  // Ignore carriage return on input.
    tty.c_iflag &= ~ICRNL;  // Translate carriage return to newline on input (unless IGNCR is set).

    tty.c_lflag = 0;                // no signaling chars, no echo,
    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 0;            // 0 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr (fd, TCSANOW, &tty) != 0) {
        HHT_LOG_DEBUG ("error %d from tcsetattr", errno);
        return -1;
    }
    return 0;
}

void MSrv_flatfrog_touch::set_blocking(int fd, int should_block)
{
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
        HHT_LOG_DEBUG ("error %d from tggetattr", errno);
        return;
    }

    tty.c_cc[VMIN]  = should_block ? 1 : 0;
    tty.c_cc[VTIME] = 0;            // 0 seconds read timeout

    if (tcsetattr (fd, TCSANOW, &tty) != 0)
        HHT_LOG_DEBUG ("error %d setting term attributes", errno);
}

speed_t MSrv_flatfrog_touch::baud_to_code(unsigned int speed)
{
    switch (speed)
    {
        case 50: return B50;
        case 75: return B75;
        case 110: return B110;
        case 134: return B134;
        case 150: return B150;
        case 200: return B200;
        case 300: return B300;
        case 600: return B600;
        case 1200: return B1200;
        case 1800: return B1800;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
        case 1000000: return B1000000;
        case 1152000: return B1152000;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        case 2500000: return B2500000;
        case 3000000: return B3000000;
        case 3500000: return B3500000;
        case 4000000: return B4000000;
        default: return B0;
    }
}

void MSrv_flatfrog_touch::serial_tcsetattr()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

void MSrv_flatfrog_touch::wait_for_char(unsigned char *c)
{
    *c = getchar();
}

void MSrv_flatfrog_touch::serial_clear_screen()
{
    HHT_LOG_DEBUG("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    HHT_LOG_DEBUG("\033[0;0H");
}

//----------------------------------------------------------------------------------------------
/*
 *******************************************************************************************
 *                      Local functions
 *******************************************************************************************
 */

/*******************************************************************************************
 * Checksum 8 Algorithm -
 * The unsigned sum of 8 bit words of data. Any overflow is added into the lowest order bit.
 */
uint8_t MSrv_flatfrog_touch::_HeaderChecksum(const RATP_header* pHeader)
{
    const uint8_t *bytes = (uint8_t *)pHeader;
    uint16_t checksum = bytes[1] + bytes[2];
    checksum = (checksum & 0xFF) + (checksum >> 8);
    return ~checksum;
}

/*******************************************************************************************
 * Checksum 16 Algorithm -
 * The unsigned sum of the 16 bit words of data. Any overflow is added into the lowest order bit.
 *  BufLen must be less than 32768
 */
uint16_t MSrv_flatfrog_touch::_Checksum16(const uint8_t* pu8Buf, uint16_t i_u16BufLen)
{
    int i;
    uint32_t u32Checksum=0;

    /* Add the even number of bytes */
    for(i = i_u16BufLen; i > 1; i -= 2)
    {
        u32Checksum +=  *pu8Buf++ << 8;   // high byte first
        u32Checksum +=  *pu8Buf++;        // low byte second
    }

    /* If the total number of data octets is odd then the last octet is
     * padded to the right (low order) with zeros to form a 16-bit word for checksum purposes.
     */
    if(i > 0)
    {
        u32Checksum += *pu8Buf << 8;
    }

    /* Wrap around the overflow. Maximum 2 cycles of overflow */
    u32Checksum = (u32Checksum & 0xFFFF) + (u32Checksum >> 16);
    u32Checksum = (u32Checksum & 0xFFFF) + (u32Checksum >> 16);

    return (uint16_t)~u32Checksum;
}

/*******************************************************************************************
 * _initTransmitter -
 *
 *  initialize the transmit block
 */
void MSrv_flatfrog_touch::_initTransmitter(Transmitter *o_pTransmitter)
{
    o_pTransmitter->eState = TX_IDLE;
    o_pTransmitter->pHead = o_pTransmitter->au8Buffer;
    o_pTransmitter->pTail = o_pTransmitter->au8Buffer;
    o_pTransmitter->pTransmit = o_pTransmitter->au8Buffer;
    memset(o_pTransmitter->au8Buffer, 0, sizeof(o_pTransmitter->au8Buffer));
}

/*******************************************************************************************
 * _flushTransmitter -
 *
 *  initialize the transmit block
 */
void MSrv_flatfrog_touch::_flushTransmitter(Transmitter *o_pTransmitter)
{
    o_pTransmitter->eState = TX_IDLE;
    o_pTransmitter->pHead = o_pTransmitter->pTransmit;
    ((TransmitBlock *)o_pTransmitter->pHead)->len = 0;
    ((TransmitBlock *)o_pTransmitter->pHead)->skip = 0;
}

/*******************************************************************************************
 * _initReceiver -
 *
 *  initialize the receiver
 */
void MSrv_flatfrog_touch::_initReceiver(Receiver *o_pReceiver)
{
    o_pReceiver->bReceiveComplete = false;
    o_pReceiver->u16RecDatagramLen = 0;
    memset(o_pReceiver->au8RecDatagram,0,sizeof(o_pReceiver->au8RecDatagram));
}

/*******************************************************************************************
 * _allocateTransmitBuffer -
 *
 *  allocates memory on transmit buffer
 */
uint8_t * MSrv_flatfrog_touch::_allocateTransmitBuffer(Transmitter *i_pTransmitter, uint8_t i_DataSize, bool bEndOfRecord)
{
    RATP_header *pRatpHdr;
    TransmitBlock *pTxHdr;

    /* Set up the transmit block header first. The pHead pointer points to the next transmit
       block. by default set skip=1, will be reset later if we do not require to wrap around.
       default we consider i_DataSize = 0.
    */
    pTxHdr = (TransmitBlock *)i_pTransmitter->pHead;
    pTxHdr->skip = 1;
    pTxHdr->len = sizeof(TransmitBlock);

    if(i_DataSize > 0)
    {
        /* we have data, add data and its checksum space to length */
        pTxHdr->len += i_DataSize + RATP_DATA_CHECKSUM_SIZE;
    }
    /* check if we have enough contigious memory on transmit buffer to hold the provided data */
    if( ( &i_pTransmitter->au8Buffer[sizeof(i_pTransmitter->au8Buffer)] - i_pTransmitter->pHead) < pTxHdr->len)
    {
        /* we do not have enough space towards end, wrap arround */
        i_pTransmitter->pHead = i_pTransmitter->au8Buffer;
        pTxHdr = (TransmitBlock *)i_pTransmitter->pHead;
        pTxHdr->skip = 0;
        pTxHdr->len += sizeof(TransmitBlock) + i_DataSize + RATP_DATA_CHECKSUM_SIZE;
    }
    else
    {
        /* we do not wrap around, reset skip */
        pTxHdr->skip = 0;
    }

    /* set the ratp header */
    pRatpHdr = &pTxHdr->RatpHdr;
    pRatpHdr->synch_leader = RATP_SYNCH_LEADER;
    pRatpHdr->control.byte = 0;
    pRatpHdr->control.bits.eor = bEndOfRecord;
    pRatpHdr->data_length = i_DataSize;

    /* increament the head */
    i_pTransmitter->pHead += pTxHdr->len;
    ((TransmitBlock *) i_pTransmitter->pHead)->len = 0;
    ((TransmitBlock *) i_pTransmitter->pHead)->skip = 0;
    return (uint8_t *)&pTxHdr->RatpHdr;
}

/*******************************************************************************************
 * _getTransmitBuffer -
 *
 *  returns the next element on queue to transmit along with its length
 */
uint8_t * MSrv_flatfrog_touch::_getTransmitBuffer(Transmitter *i_pTransmitter, uint16_t *o_pu16Length)
{
    TransmitBlock *pTxHdr;

    pTxHdr = (TransmitBlock *)i_pTransmitter->pTransmit;
    if(pTxHdr->skip)
    {
        pTxHdr->skip = 0;
        /* wrap around */
        i_pTransmitter->pTransmit = i_pTransmitter->au8Buffer;
        pTxHdr = (TransmitBlock *)i_pTransmitter->pTransmit;
    }
    /* default to zero */
    *o_pu16Length = 0;
    if(pTxHdr->len >=2)
    {
        *o_pu16Length = pTxHdr->len - 2;
    }
    return (uint8_t *)&pTxHdr->RatpHdr;
}

/*******************************************************************************************
 * _BuildTransmitPacket -
 *
 *
 */
void MSrv_flatfrog_touch::_BuildTransmitPacket(uint8_t *io_pHead, const uint8_t * i_pu8DataBuf, uint8_t i_u16DataSize)
{
    uint8_t *u8DataPtr;
    uint16_t u16CheckSum;
    uint8_t *u8ChecksumPtr;

    u8DataPtr = io_pHead + RATP_HEADER_SIZE;
    memcpy(u8DataPtr,i_pu8DataBuf,i_u16DataSize);
    u16CheckSum = _Checksum16(i_pu8DataBuf,i_u16DataSize);
    u8ChecksumPtr = u8DataPtr + i_u16DataSize;
    u8ChecksumPtr[0] = u16CheckSum >> 8;
    u8ChecksumPtr[1] = u16CheckSum & 0xFF;
}

/*******************************************************************************************
 * _pushTransmitBuffer -
 *
 *  This function will accept user provided data, chunk it to transmit packets if needed and
 *  store on transmit buffer. The transmit packet size is RATP_MAX_PACKET_SIZE + 2 byte header.
 */
bool MSrv_flatfrog_touch::_pushTransmitBuffer(Transmitter *i_pTransmitter,const uint8_t *i_pu8DataBuf, uint16_t i_u16BufLen)
{
    bool bEndOfRecord;
    uint8_t * pDataPtr;
    uint8_t u8MDLPackets;
    uint8_t u8RemainderBytes;
    uint16_t u16SizeofDataPacket;
    uint16_t u16SpaceAvail;
    uint16_t u16SpaceNeeded;

    /* first calculate the number of transmit packets needed to hold the provided data.
       u8MDLPackets are the number of MDL size packets and u8RemainderBytes is remainder
    */

    u8MDLPackets = i_u16BufLen/RATP_MDL_SIZE;
    u8RemainderBytes = i_u16BufLen%RATP_MDL_SIZE;

    /* calculate size of remaining data packet */
    if(u8RemainderBytes)
    {
        u16SizeofDataPacket = sizeof(TransmitBlock) + u8RemainderBytes + RATP_DATA_CHECKSUM_SIZE;
    }
    else
    {
        u16SizeofDataPacket = 0;
    }

    /* calculate the total space needed hold the user data */
    u16SpaceNeeded = ((u8MDLPackets * RATP_SIZEOF_MDL_TRANSMIT_BLOCK) + u16SizeofDataPacket);

    /* make sure head doesn't run into tail, leave one extra block header to mark the head */
    u16SpaceNeeded += 1 + sizeof(TransmitBlock);

    /* check the space available on transmit buff */
    if(i_pTransmitter->pTail > i_pTransmitter->pHead)
    {
        u16SpaceAvail = i_pTransmitter->pTail - i_pTransmitter->pHead;
        if(u16SpaceAvail < u16SpaceNeeded)
        {
            return false;
        }
    }
    else
    {
        u16SpaceAvail = &i_pTransmitter->au8Buffer[sizeof(i_pTransmitter->au8Buffer)] - i_pTransmitter->pHead;

        if(u16SpaceAvail < u16SpaceNeeded)
        {
            /* This is going to wrap around the end of the buffer, account for the "skip" space */
            u16SpaceNeeded -= u16SpaceAvail / RATP_SIZEOF_MDL_TRANSMIT_BLOCK * RATP_SIZEOF_MDL_TRANSMIT_BLOCK;
            u16SpaceAvail = i_pTransmitter->pTail - i_pTransmitter->au8Buffer;
            if(u16SpaceAvail < u16SpaceNeeded)
            {
                return false;
            }
        }
    }
    bEndOfRecord = false;
    while(u8MDLPackets > 0)
    {
        if(u8RemainderBytes==0)
        {
            if(u8MDLPackets == 1)
            {
                bEndOfRecord = true;
            }
        }
        pDataPtr = _allocateTransmitBuffer(i_pTransmitter,RATP_MDL_SIZE, bEndOfRecord);
        _BuildTransmitPacket(pDataPtr, i_pu8DataBuf,RATP_MDL_SIZE);
        u8MDLPackets--;
        i_pu8DataBuf = i_pu8DataBuf +  RATP_MDL_SIZE;
    }

    /* now send the remaining data */
    if(u8RemainderBytes > 0)
    {
        /* if we are here this should be end of record */
        pDataPtr = _allocateTransmitBuffer(i_pTransmitter,u8RemainderBytes, true);
        _BuildTransmitPacket(pDataPtr, i_pu8DataBuf,u8RemainderBytes);
    }
    return true;
}

/*******************************************************************************************
 * _advanceTransmitBuffer -
 *
 *  advances the transmit pointer
 */
void MSrv_flatfrog_touch::_advanceTransmitBuffer(Transmitter *io_pTransmitter)
{
    uint16_t u16Size;
    TransmitBlock *pTxHdr;

    if(io_pTransmitter->pTransmit == io_pTransmitter->pHead)
    {
        return;
    }

     pTxHdr = (TransmitBlock *)io_pTransmitter->pTransmit;
    /* check if we are on a skip block */
    if(pTxHdr->skip)
    {
        pTxHdr->skip = 0;
        /* wrap around */
        io_pTransmitter->pTransmit = io_pTransmitter->au8Buffer;
        pTxHdr = (TransmitBlock *)io_pTransmitter->pTransmit;
    }
    else
    {
        u16Size = pTxHdr->len;
        /* clear the memory */
        pTxHdr->len = 0;
        io_pTransmitter->pTransmit += u16Size;

        /* check if we have progressed to skip block. if yes, skip the block */
        pTxHdr = (TransmitBlock *)io_pTransmitter->pTransmit;
        if(pTxHdr->skip)
        {
            pTxHdr->skip = 0;
            /* wrap around */
            io_pTransmitter->pTransmit = io_pTransmitter->au8Buffer;
            pTxHdr = (TransmitBlock *)io_pTransmitter->pTransmit;
        }
    }
}

/*******************************************************************************************
 * _popTransmitBuffer -
 *
 *  frees the memory that was owned by the sendCallback (the previously transmitted block) back
 *  for our use.
 */
void MSrv_flatfrog_touch::_popTransmitBuffer(Transmitter *io_pTransmitter)
{
    io_pTransmitter->pTail =  io_pTransmitter->pTransmit;
}

/*******************************************************************************************
 * _lookAheadTransmitBuffer -
 *
 *  peek ahead in the transmit queue to see if data is available. if yes return truw, else false.
 */
bool MSrv_flatfrog_touch::_lookAheadTransmitBuffer(Transmitter *io_pTransmitter)
{
    TransmitBlock *pTxHdr;
    pTxHdr = (TransmitBlock *)io_pTransmitter->pTransmit;

    /* check if we are = head */
    if(pTxHdr ==  (TransmitBlock *)io_pTransmitter->pHead)
    {
        return false;
    }
    else
    {
        return true;
    }

}

/******************************************************************************
 * Update estimate of round trip time when an ack for the last sent packet
 *   arrives
 */
void MSrv_flatfrog_touch::_updateRTTGood(RATP_instance * io_pInstance)
{
    /* Update is (1-a)RTT + aCurrent, where a is 1/16, RTT is in q6 but the retryTimeoutCtr is in q0 */
    io_pInstance->u16q6RoundTripTime = (io_pInstance->u16q6RoundTripTime * 15 + io_pInstance->u16RetryTimeoutCtr * 64) >> 4;
    if(io_pInstance->u16q6RoundTripTime < io_pInstance->u16q6MinRoundTripTime)
    {
        io_pInstance->u16q6RoundTripTime = io_pInstance->u16q6MinRoundTripTime;
    }
    else if(io_pInstance->u16q6RoundTripTime > RATP_MAX_ROUND_TRIP_TIME_MULT * io_pInstance->u16q6MinRoundTripTime)
    {
        io_pInstance->u16q6RoundTripTime = RATP_MAX_ROUND_TRIP_TIME_MULT * io_pInstance->u16q6MinRoundTripTime;
    }
}

/******************************************************************************
 * Update estimate of round trip time when no ack arrives in time
 */
void MSrv_flatfrog_touch::_updateRTTMiss(RATP_instance * io_pInstance)
{
    /* assume the round trip time is too short in this case, so increase it.
     *   This assumes the error rate is low, so it will have time to recover
     *   a good estimate of the round trip time.
     */
    if(io_pInstance->u16q6RoundTripTime < RATP_MAX_ROUND_TRIP_TIME_MULT * io_pInstance->u16q6MinRoundTripTime - 8)
    {
        io_pInstance->u16q6RoundTripTime += 8;
    }
    else
    {
        io_pInstance->u16q6RoundTripTime = RATP_MAX_ROUND_TRIP_TIME_MULT * io_pInstance->u16q6MinRoundTripTime;
    }
}

/******************************************************************************
 * Convert round trip time into a retry timeout in ticks
 */
uint16_t MSrv_flatfrog_touch::_getRetryTimeout(RATP_instance * io_pInstance)
{
    /* 4 times RTT, min 2 (so RTT min is .5) */
    return io_pInstance->u16q6RoundTripTime >> 4;
}

/******************************************************************************
 * Try to Send when an ACK is expected and this packet hasn't been sent yet
 */
bool MSrv_flatfrog_touch::_trySendAckExpected(RATP_instance * io_pInstance)
{
    uint16_t u16SendDataSize = 0;
    uint8_t *pu8TxPkt = _getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);

    /* Attempt to send */
    if(io_pInstance->send_callback(io_pInstance->pUserHandle, pu8TxPkt, u16SendDataSize))
    {
        /* Success - free previous buffer, start waiting for the ACK */
        _popTransmitBuffer(&io_pInstance->stTransmitter);
        io_pInstance->stTransmitter.eState = TX_WAIT_FOR_ACK;
        io_pInstance->u16RetryTimeoutCtr = 0;
        io_pInstance->u16SendFailCtr = 0;
        io_pInstance->u16UserTimeoutCtr = 0;
        STAT(io_pInstance, sendCallbackSuccess);
        return true;
    }
    else if(RATP_MAX_SEND_FAIL_TICKS < io_pInstance->u16SendFailCtr++)
    {
        /* Failed to send many times in a row, give up */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eState = CLOSED;
        io_pInstance->eRatpEventHappened = RATP_ERROR;
    }
    else
    {
        io_pInstance->stTransmitter.eState = TX_SEND_ACK_EXPECTED;
        STAT(io_pInstance, sendCallbackReject);
    }
    return false;
}

/******************************************************************************
 * Try to Send when an ACK is NOT expected and this packet hasn't been sent yet
 */
bool MSrv_flatfrog_touch::_trySendAckNotExpected(RATP_instance *io_pInstance)
{
    uint16_t u16SendDataSize = 0;
    uint8_t *pu8TxPkt = _getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);

    /* Attempt to send */
    if(io_pInstance->send_callback(io_pInstance->pUserHandle, pu8TxPkt, u16SendDataSize))
    {
        /* Success - free previous buffer and don't send this buffer again or wait for ACK */
        _popTransmitBuffer(&io_pInstance->stTransmitter);
        _advanceTransmitBuffer(&io_pInstance->stTransmitter);
        io_pInstance->stTransmitter.eState = TX_IDLE;
        io_pInstance->u16SendFailCtr = 0;
        STAT(io_pInstance, sendCallbackSuccess);
        return true;
    }
    else if(RATP_MAX_SEND_FAIL_TICKS < io_pInstance->u16SendFailCtr++)
    {
        /* Failed to send many times in a row, give up */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eState = CLOSED;
        io_pInstance->eRatpEventHappened = RATP_ERROR;
    }
    else
    {
        io_pInstance->stTransmitter.eState = TX_SEND_NO_ACK_EXPECTED;
        STAT(io_pInstance, sendCallbackReject);
    }
    return false;
}

/*******************************************************************************************
 * _Transmit -
 *
 *  function to send RATP packet to lower layer and update transmit pointers
 */
void MSrv_flatfrog_touch::_Transmit(RATP_instance * io_pInstance)
{
    if(io_pInstance->stTransmitter.eState == TX_SEND_ACK_EXPECTED)
    {
        _trySendAckExpected(io_pInstance);
    }
    else if(io_pInstance->stTransmitter.eState == TX_SEND_NO_ACK_EXPECTED)
    {
        _trySendAckNotExpected(io_pInstance);
    }
}

/*******************************************************************************************
 * Takes bytes received on the wire and assembles and verifies up to 1 complete packet.
 * Indicates how many bytes were used and whether a packet is complete.
 * Packet header is stored in PktParser, Data payload is stored in the supplied data buffer
 *
 * Packet reception consists of three parts.
 * 1. Synch Detection  - Received bytes are searched for Synch Pattern
 * 2. Header Reception - Once the synch is found, verify the header.
 * 3. Data Reception   - Once the header is verified get/save data bytes and verify the data.
 * On successful verification of data, the complete packet is provided to the next stage.
 */
bool MSrv_flatfrog_touch::_PacketReception(             // TRUE: a complete packet is available. False: waiting for more bytes
    RATP_instance * io_pInstance,  // Instance for stats
    PktParser *io_pstPktParse,     // Parser state
    const uint8_t* i_pu8InBuf,     // input bytes from the wire
    uint16_t i_u16BufLen,          // number of bytes from the wire
    uint8_t* o_pu8PayloadBuf,      // Buffer to collect payload into, should be the same until a complete packet
                                   //   is received. Space for RATP_MDL_SIZE + RATP_DATA_CHECKSUM_SIZE. Must be
                                   //   contiguous with previously received payload chunks if present.
    uint16_t *o_pu16BytesConsumed) // Number of bytes of input consumed
{
    uint16_t u16CalcCheckSum;
    uint16_t u16RxCheckSum;
    uint16_t u16BytesRemaining;
    const uint8_t *pu8DataPtr = i_pu8InBuf;
    uint8_t *pu8Header = (uint8_t *) (&io_pstPktParse->sHeader) + 1;  // Point after synch header
    int i, j;
    bool bSynchFound;
    uint16_t u16BytesToCopy;

    u16BytesRemaining = i_u16BufLen;

    while(u16BytesRemaining)
    {
        switch(io_pstPktParse->eState)
        {
        case SYNCH_DETECTION:
            /* Find the synch byte in the stream
             */
            u16BytesRemaining--;
            if(RATP_SYNCH_LEADER == *pu8DataPtr++)
            {
                io_pstPktParse->eState = HEADER_RECEPTION;
                io_pstPktParse->u8PartHeaderLen = 0;
            }
            break;

        case HEADER_RECEPTION:
            /* Case where only part of the header has been received. In this case the received data is
             * saved untill we have enough data to verify the header. No further processing can continue
             * until the complete header has been read and verified.
             */
            u16BytesRemaining--;
            pu8Header[io_pstPktParse->u8PartHeaderLen++] = *pu8DataPtr++;
            if(io_pstPktParse->u8PartHeaderLen >= RATP_HDR_CS_DATA_SIZE)
            {
                if(io_pstPktParse->sHeader.header_checksum == _HeaderChecksum(&io_pstPktParse->sHeader))
                {
                    /* If either of the RST, SO, or FIN flags are set then legally the entire packet
                       has already been read and it is considered to have 'arrived'.  No data portion of a
                       packet is present when one of those flags is set.
                    */
                    STAT(io_pInstance, rxHeaderOkay);
                    if( io_pstPktParse->sHeader.control.bits.rst ||
                        io_pstPktParse->sHeader.control.bits.fin ||
                        io_pstPktParse->sHeader.data_length == 0 )
                    {
                        io_pstPktParse->eState = PACKET_RECEIVED;
                        *o_pu16BytesConsumed = i_u16BufLen - u16BytesRemaining;
                        io_pstPktParse->u16RecPayloadLen = 0;
                        return true;
                    }
                    else
                    {
                        io_pstPktParse->u16RecPayloadLen = 0;
                        io_pstPktParse->eState = DATA_RECEPTION;
                    }
                }
                else
                {
                    /* header check failed, search the copied bytes for a synch */
                    STAT(io_pInstance, rxHeaderFail);
                    bSynchFound = false;
                    for(i = 0; i < RATP_HDR_CS_DATA_SIZE; i++)
                    {
                        if(RATP_SYNCH_LEADER == pu8Header[i])
                        {
                            bSynchFound = true;
                            break;
                        }
                    }
                    if(bSynchFound)
                    {
                        i++;    // skip the synch leader
                        for(j = i; j < RATP_HDR_CS_DATA_SIZE; j++)
                        {
                            pu8Header[j-i] = pu8Header[j];
                        }
                        io_pstPktParse->u8PartHeaderLen = RATP_HDR_CS_DATA_SIZE - i;
                        /* continue in HEADER_RECEPTION state, with what is left of the shifted header */
                    }
                    else
                    {
                        io_pstPktParse->eState = SYNCH_DETECTION;
                    }
                }
            }
            break;

        case DATA_RECEPTION:
            /* A complete header has been received and verified to have a payload. Collecting the payload */

            /* Number of bytes remaining in this packet */
            u16BytesToCopy = io_pstPktParse->sHeader.data_length + RATP_DATA_CHECKSUM_SIZE - io_pstPktParse->u16RecPayloadLen;
            u16BytesToCopy = u16BytesToCopy > u16BytesRemaining ? u16BytesRemaining : u16BytesToCopy;
            /* Append these bytes to the payload */
            memcpy(o_pu8PayloadBuf + io_pstPktParse->u16RecPayloadLen, pu8DataPtr, u16BytesToCopy);
            io_pstPktParse->u16RecPayloadLen += u16BytesToCopy;

            if(io_pstPktParse->u16RecPayloadLen == io_pstPktParse->sHeader.data_length + RATP_DATA_CHECKSUM_SIZE)
            {
                /* valadite the data */
                u16CalcCheckSum = _Checksum16(o_pu8PayloadBuf, io_pstPktParse->sHeader.data_length);
                u16RxCheckSum = o_pu8PayloadBuf[io_pstPktParse->sHeader.data_length] << 8 | o_pu8PayloadBuf[io_pstPktParse->sHeader.data_length + 1];
                if(u16CalcCheckSum == u16RxCheckSum)
                {
                    /* data verified, complete packet received */
                    STAT(io_pInstance, rxDataOkay);
                    io_pstPktParse->u16RecPayloadLen = io_pstPktParse->sHeader.data_length;
                    u16BytesRemaining -= u16BytesToCopy;
                    pu8DataPtr += u16BytesToCopy;

                    io_pstPktParse->eState = PACKET_RECEIVED;
                    *o_pu16BytesConsumed = i_u16BufLen - u16BytesRemaining;
                    return true;
                }
                else
                {
                    /* data checksum failed - this is a bad packet, Ignore it. A bad packet is received
                       when it fails either the header or data checksum tests.  When this happens the
                       sender will retransmit the packet after the retransmission timeout interval.

                       NOTE: we have NOT advanced u16BytesRemaining or pu8DataPtr, so we'll start searching for synch at the beginning of the last input
                       It would be a very strange event to drop more than a byte or two from the verified length of the header, so searching the previous
                       input isn't worth the effort.
                    */
                    STAT(io_pInstance, rxDataFail);
                    io_pstPktParse->eState = SYNCH_DETECTION;
                }
            }
            else
            {
                u16BytesRemaining -= u16BytesToCopy;
                pu8DataPtr += u16BytesToCopy;
            }
            break;

        case PACKET_RECEIVED:
        default:
            /* A packet was received last time, restart at the beginning */
            io_pstPktParse->eState = SYNCH_DETECTION;
            break;
        }
    }

    /* Used all the bytes, waiting for more */
    *o_pu16BytesConsumed = i_u16BufLen;
    return false;
}


/******************************************************************************
 * Build an ACK packet, with Data if it is available, next in line on the transmit
 *   queue. Update state.
 */
void MSrv_flatfrog_touch::_buildAckDataPacket(RATP_instance * io_pInstance)
{
    uint16_t u16SendDataSize;
    RATP_header *pTxHdr = (RATP_header *)_getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);
    if(u16SendDataSize == 0)
    {
        pTxHdr = (RATP_header *)_allocateTransmitBuffer(&io_pInstance->stTransmitter, RATP_HEADER_ONLY,false);
    }
    if(u16SendDataSize > RATP_HEADER_SIZE)
    {
        STAT(io_pInstance, buildDataPacket);
        io_pInstance->stTransmitter.eState = TX_SEND_ACK_EXPECTED;
        io_pInstance->bSNUsed = true;
    }
    else
    {
        STAT(io_pInstance, buildAckPacket);
        io_pInstance->stTransmitter.eState = TX_SEND_NO_ACK_EXPECTED;
    }
    pTxHdr->control.bits.ack = 1;
    pTxHdr->control.bits.sn = io_pInstance->u8CurSN;
    pTxHdr->control.bits.an = io_pInstance->u8CurAN;
    pTxHdr->header_checksum = _HeaderChecksum(pTxHdr);
}

/******************************************************************************
 * Build a SYN packet, possibly with ACK, on an existing buffer in the queue,
 * if available, or new otherwise. Update transmit state.
 */
void MSrv_flatfrog_touch::_buildSynPacket(RATP_instance * io_pInstance, bool i_withAck)
{
    uint16_t u16SendDataSize;
    RATP_header *pTxHdr = (RATP_header *)_getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);
    if(u16SendDataSize == 0)
    {
        pTxHdr = (RATP_header *)_allocateTransmitBuffer(&io_pInstance->stTransmitter, RATP_HEADER_ONLY,false);
    }

    pTxHdr->control.byte = 0;
    pTxHdr->control.bits.sn = io_pInstance->u8CurSN;
    pTxHdr->control.bits.syn = 1;
    if(i_withAck)
    {
        pTxHdr->control.bits.an = io_pInstance->u8CurAN;
        pTxHdr->control.bits.ack = 1;
    }
    pTxHdr->data_length = 0;  // no options supported yet
    pTxHdr->header_checksum = _HeaderChecksum(pTxHdr);

    io_pInstance->bSNUsed = true;
    io_pInstance->stTransmitter.eState = TX_SEND_ACK_EXPECTED;
}

/******************************************************************************
 * Build a FIN packet, possibly with ACK, on an existing buffer in the queue,
 * if available, new otherwise. Update transmit state.
 */
void MSrv_flatfrog_touch::_buildFinPacket(RATP_instance * io_pInstance, bool i_withAck)
{
    uint16_t u16SendDataSize;
    RATP_header *pTxHdr = (RATP_header *)_getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);
    if(u16SendDataSize == 0)
    {
        pTxHdr = (RATP_header *)_allocateTransmitBuffer(&io_pInstance->stTransmitter, RATP_HEADER_ONLY,false);
    }

    pTxHdr->control.byte = 0;
    pTxHdr->control.bits.sn = io_pInstance->u8CurSN;
    pTxHdr->control.bits.fin = 1;
    if(i_withAck)
    {
        pTxHdr->control.bits.an = io_pInstance->u8CurAN;
        pTxHdr->control.bits.ack = 1;
    }
    pTxHdr->data_length = 0;
    pTxHdr->header_checksum = _HeaderChecksum(pTxHdr);

    io_pInstance->bSNUsed = true;
    io_pInstance->stTransmitter.eState = TX_SEND_ACK_EXPECTED;
}

/******************************************************************************
 * Build a RST packet, possibly with ACK, on an existing buffer in the queue,
 * if available, new otherwise. Update transmit state.
 */
void MSrv_flatfrog_touch::_buildRstPacket(RATP_instance * io_pInstance, bool i_withAck)
{
    uint16_t u16SendDataSize;
    RATP_header *pTxHdr = (RATP_header *)_getTransmitBuffer(&io_pInstance->stTransmitter, &u16SendDataSize);
    if(u16SendDataSize == 0)
    {
        pTxHdr = (RATP_header *)_allocateTransmitBuffer(&io_pInstance->stTransmitter, RATP_HEADER_ONLY,false);
    }

    pTxHdr->control.byte = 0;
    pTxHdr->control.bits.sn = io_pInstance->u8CurSN;
    pTxHdr->control.bits.rst = 1;
    if(i_withAck)
    {
        pTxHdr->control.bits.an = io_pInstance->u8CurAN;
        pTxHdr->control.bits.ack = 1;
    }
    pTxHdr->data_length = 0;
    pTxHdr->header_checksum = _HeaderChecksum(pTxHdr);
    io_pInstance->stTransmitter.eState = TX_SEND_NO_ACK_EXPECTED;
}

/******************************************************************************
 * Do Procedure C0 or C2 from the RFC, this is the opening to multiple responses.
 * Return true if this procedure has handled the packet and no further processing
 * is required
 */
bool MSrv_flatfrog_touch::_doProcedureC0C2(RATP_instance * io_pInstance, bool i_bDoC0)
{
    /* -------------------------------- CO or C2 -------------------------------------- */
    /* Examine the received SN field value. If the SN value was expected then return and
       continue the processing associated with this state.
    */
    if(io_pInstance->stPktParser.sHeader.control.bits.sn == io_pInstance->u8CurAN)
    {
        return false;
    }

    /* sn != ackNum -- duplicate */
    if(io_pInstance->stPktParser.sHeader.control.bits.rst ||
       io_pInstance->stPktParser.sHeader.control.bits.fin)
    {   /* sn != ackNum, rst=1 or fin=1 */
        /* discard the current packet and return to current state without any further processing. */
    }
    /* This is the only place C0 and C2 differ */
    else if(i_bDoC0 &&
            io_pInstance->stPktParser.sHeader.control.bits.syn == true &&
            io_pInstance->stPktParser.sHeader.control.bits.ack == true &&
            io_pInstance->stPktParser.sHeader.control.bits.an == io_pInstance->u8CurSN)
    {
        /* C0 only:  If SYN and ACK are set, and AN == curSN, then this is a duplicate of the
         *    SYN-ACK, acking our original SYN. Send an ACK and any data available:
         * <SN=curSN><AN=curAN><CTL=ACK><Data>
         *   if data is included, set SNUsed true, else false.
         * return to the current state without any further processing.
         */
        _buildAckDataPacket(io_pInstance);
    }
    else if(io_pInstance->stPktParser.sHeader.control.bits.syn)
    {   /* sn != ackNum, syn=1, rst=0 & fin=0 */
        /* we assume that the other end crashed and has attempted to open a new connection.
         * we respond by sending a legal reset: set curSN = received AN, curAN = received SN+1 modulo 2
         * <SN=curSN><AN=curSN><CTL=RST, ACK>
         * This will cause the other end, currently in the SYN-SENT state to close.
         */
        io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
        io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
        io_pInstance->bSNUsed = false;

        /* Flush the retransmission queue, inform the user, discard the packet, delete the
         * TCB, and go to the CLOSED state without any further processing.
         */
        _flushTransmitter(&io_pInstance->stTransmitter);
        _buildRstPacket(io_pInstance, true);
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
        io_pInstance->eState = CLOSED;
    }
    else
    {
        /* If ACK is set and received AN = curSN+1 modulo 2 and SNused is true, then this is an
         * acknowledgement of our previous transmitted data. Free the retransmission, increment
         * curSN + 1 modulo 2 and set SNused = false.
         */
        if(io_pInstance->stPktParser.sHeader.control.bits.ack &&
           io_pInstance->stPktParser.sHeader.control.bits.an != io_pInstance->u8CurSN &&
           io_pInstance->bSNUsed)
        {
            io_pInstance->u8CurSN ^=1;
            io_pInstance->bSNUsed = false;
            _advanceTransmitBuffer(&io_pInstance->stTransmitter);
            STAT(io_pInstance, rxDupWithNewAck);
        }

        /* It is assumed that this packet is a duplicate of one already received. Send an ACK back,
         * discard the duplicate packet and return to the current state without further processing.
         * <SN=curSN><AN=curAN><CTL=ACK>
         * if there is data available, include it and set SNused true.
         */
        _buildAckDataPacket(io_pInstance);
        if(io_pInstance->stPktParser.sHeader.data_length > 0)
        {
            STAT(io_pInstance, rxDupData);
        }
    }
    return true;
}

/******************************************************************************
 * Procedure E handles SYN packets in unexpected places.
 * return true if the packet has been handled and no further processing is required.
 */
bool MSrv_flatfrog_touch::_doProcedureE(RATP_instance * io_pInstance)
{
    /* -------------------------------- E -------------------------------------- */
    if(!io_pInstance->stPktParser.sHeader.control.bits.syn)
    {
        return false;
    }

    /* The presence of a SYN here is an error.  Flush the retransmission queue, send a RST packet. */
    _flushTransmitter(&io_pInstance->stTransmitter);
    if(io_pInstance->stPktParser.sHeader.control.bits.ack)
    {
        /* If the ACK flag was set then send:  <SN=received AN><CTL=RST> */
        io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
    }
    else
    {
        /* If the ACK flag was not set then send: <SN=0><CTL=RST> */
        io_pInstance->u8CurSN = 0;
    }
    _buildRstPacket(io_pInstance, false);
    io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
    io_pInstance->eState = CLOSED;
    return true;
}

/*******************************************************************************************
 * _respondSynSent -
 *
 *  This represents waiting for a matching connection request after having sent a connection
 *  request.
 *  Note: we always have two buffer allocated - one for the SYN (which is retrying) and one
 *   for our syn ack. If we leave to ESTABLISHED, drop both. If we leave to SYN-RECEIVED
 *   drop only one.
 */
void MSrv_flatfrog_touch::_respondSynSent(RATP_instance * io_pInstance)
{
    /*  This state is entered when this end of the connection execute an active OPEN.
        we only sheck for ack, syn and rst flags in this state. we ignore the rest.
    */

    /* -------------------------------- B -------------------------------------- */

    if(io_pInstance->stPktParser.sHeader.control.bits.ack)
    {   /* ack=1 */
        if(io_pInstance->stPktParser.sHeader.control.bits.an == !(io_pInstance->u8CurSN))
        {   /* ack=1, an=expected */
            if(io_pInstance->stPktParser.sHeader.control.bits.rst)
            {   /* ack=1, an=expected, rst=1  */

                /* we have received a reset, discard current packet and flush Tx queue,
                   inform the user/application and go to the CLOSED state without any
                   further processing.
                */
                _flushTransmitter(&io_pInstance->stTransmitter);
                io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
                io_pInstance->eState = CLOSED;
            }
            else if(io_pInstance->stPktParser.sHeader.control.bits.syn)
            {   /*  syn=1, ack=1, an=expected ack */
                /*  our SYN has been acknowledged by peer. at the time of sending our syn we had allocated extra space
                    to handle case where we sent syn and received a syn and we have data already on transmit buffer */
                _advanceTransmitBuffer(&io_pInstance->stTransmitter);
                _advanceTransmitBuffer(&io_pInstance->stTransmitter);
                io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
                io_pInstance->u8CurSN ^= 1;
                io_pInstance->bSNUsed = false;

                /* Any payload is an OPTIONS field - we don't yet support any options so ignore them */

                _buildAckDataPacket(io_pInstance);

                /* go to the ESTABLISHED state without any further processing. */
                io_pInstance->eState = ESTABLISHED;
                io_pInstance->eRatpEventHappened = RATP_OPENED;
            }   /*  ack=1, syn=1, an=expected ack ... case ends */
        }   /* ack=1, an=expected */
        else
        {   /* ack=1, an != expected */
            if(io_pInstance->stPktParser.sHeader.control.bits.rst)
            {   /* ack=1, an != expected, rst=1 */
                /* discard packet and returning to current state without any
                   further processing
                */
            }
            else
            {   /* ack=1, an != expected, rst=0 */

                /* send reset, discard current packet and return to current state without further
                 * processing. we are going to send reset. reuse the current send buffer - this
                 * will have to be rebuilt with a SYN after sending - see _tick
                 * <SN=received AN><CTL=RST>
                 */
                io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
                _buildRstPacket(io_pInstance, false);
                io_pInstance->u8CurSN = 0;
            }
        }
    } /* ack=1   ... case ends */
    else
    {   /* ack=0 */
        if(io_pInstance->stPktParser.sHeader.control.bits.rst)
        { /* ack=0 , rst=1 */
              /* discard the packet and return to this state without further processing */
        }
        else if(io_pInstance->stPktParser.sHeader.control.bits.syn)
        {   /* ack=0, rst=0, syn=1 */
            /* The other end of the connection has executed an active open also.
             * Acknowledge the SYN, set our MDL, and send:
             * <SN=0><AN=received SN+1 modulo 2><CTL=SYN, ACK><LENGTH=MDL>
             * Move to syn received state - advance once to leave one buffer in that state
             */
            io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
            _advanceTransmitBuffer(&io_pInstance->stTransmitter);
            _buildSynPacket(io_pInstance, true);
            io_pInstance->eState = SYN_RECEIVED;

            /* Any payload is an OPTIONS field - we don't yet support any options so ignore them */
        }
    }
}

/*******************************************************************************************
 * _respondSynReceived -
 *
 *  This represents waiting for a confirming connection request acknowledgment after having
 *  both received and sent a connection request.
 * Note: we have one buffer allocated for the SYN-ACK - advance before leaving
 */
void MSrv_flatfrog_touch::_respondSynReceived(RATP_instance * io_pInstance)
{
    /* -------------------------------- C1 -------------------------------------- */
    /* Examine the received SN field value.  If the SN value was expected (== curAN) then if
     * there is data, set curAN = received SN+1 modulo 2. Return and continue the processing
     * associated with this state.
     */
    if(io_pInstance->stPktParser.sHeader.control.bits.sn != io_pInstance->u8CurAN)
    {   /* sn != expected */
        /* SN value was not what was expected! */
        if( io_pInstance->stPktParser.sHeader.control.bits.rst == true ||
            io_pInstance->stPktParser.sHeader.control.bits.fin == true)
        {
            /* discard the packet and return to the current state without any further processing.*/
        }
        else if(io_pInstance->stPktParser.sHeader.control.bits.syn == true &&
                io_pInstance->stPktParser.sHeader.control.bits.ack == true &&
                io_pInstance->stPktParser.sHeader.control.bits.an != io_pInstance->u8CurSN)
        {
            /* If SYN and ACK are set, and AN == curSN+1mod2, then this is a simultaneous open SYN-ACK
             * acking our original SYN. Go to ESTABLISHED state, Increment curSN+1mod2, Send an ACK and
             * any data available:
             * <SN=curSN><AN=curAN><CTL=ACK><Data>
             * if data is included, set SNUsed true, else false.
             */
            io_pInstance->u8CurSN ^= 1;
            io_pInstance->bSNUsed = false;

            /* The syn is acked - discard our syn-ack buffer and build with possible data */
            _advanceTransmitBuffer(&io_pInstance->stTransmitter);
            _buildAckDataPacket(io_pInstance);

            /* Any payload is an OPTIONS field - we don't yet support any options so ignore them */

            io_pInstance->eRatpEventHappened = RATP_OPENED;
            io_pInstance->eState = ESTABLISHED;
        }
        else if(io_pInstance->stPktParser.sHeader.control.bits.syn == true &&
                io_pInstance->stPktParser.sHeader.control.bits.ack == false)
        {
            /* If SYN only is set, then this is a retry of the other side's SYN, indicating that our SYN and SYN-ACK
             * were lost. Resend our SYN-ACK in response.
             * <SN=0><AN=curAN><CTL=SYN,ACK>
             */
            _buildSynPacket(io_pInstance, true);

            /* Any payload is an OPTIONS field - we don't yet support any options so ignore them */
        }
        else
        {
            /* Otherwise, this is an unexpected packet and the other end is not in the expected state. Send a reset
             * <SN=0><CTL=RST>
             * The user should receive the message "Error: Connection reset.", then delete the TCB and go to the
             * CLOSED state without any further processing.
             */
            _flushTransmitter(&io_pInstance->stTransmitter);
            _buildRstPacket(io_pInstance, false);
            io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
            io_pInstance->eState = CLOSED;
        }
    }
    else
    {
        /* -------------------------------- D1 -------------------------------------- */
        /* The packet is examined for a RST flag.  If RST is not set then return and continue
           the processing associated with this state. */
        if(io_pInstance->stPktParser.sHeader.control.bits.rst)
        {
            /* discard the packet, flush the retransmission queue, inform the user/application
               and go to the CLOSED state without any further processing.
            */
            _flushTransmitter(&io_pInstance->stTransmitter);
            io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
            io_pInstance->eState = CLOSED;
        }
        /* -------------------------------- E -------------------------------------- */
        else if(_doProcedureE(io_pInstance))
        {
            /* Unexpected SYN packet has been handled */
        }
        else
        {
            /* if there is data increment curAN */
            if(io_pInstance->stPktParser.sHeader.data_length > 0)
            {
                io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
            }

            /* -------------------------------- F1 -------------------------------------- */
            /* Check the presence of the ACK flag. If ACK is not set then discard the packet and
               return without any further processing.
            */
            if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
            {
                /* Ignore packet */
            }
            else if(io_pInstance->stPktParser.sHeader.control.bits.an == io_pInstance->u8CurSN)
            {
                /* ACK flag was set and the AN field value is unexpected & connection was initiated
                 * actively. inform the user "Error: Connection refused", flush the retransmission
                 * queue, discard the packet, and send a legal RST packet:
                 * <SN=received AN><CTL=RST>
                 */
                _flushTransmitter(&io_pInstance->stTransmitter);
                io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
                _buildRstPacket(io_pInstance, false);
                io_pInstance->eState = CLOSED;
                io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
            }
            else
            {
                /* If the AN field value is curSN+1 modulo 2 and SNused is true, this is an acknowledgement
                 * of our previously sent data. Free the retransmission, increment curSN+1 modulo 2 and set
                 * SNused = false and continue the processing associated with this state.
                 */
                io_pInstance->u8CurSN ^= 1;
                io_pInstance->bSNUsed = false;

                /* -------------------------------- H1 -------------------------------------- */
                /* Our SYN has been acknowledged.  At this point we are technically in the ESTABLISHED
                   state. Send any initial data which is queued to send

                   Go to the ESTABLISHED state and execute procedure I1 to process any data which might
                   be in this packet. Any packet not satisfying the above tests is discarded and ignored.
                   Return to the current state without any further processing.
                */

                /* we have received an ack, advance transmit buffer */
                _advanceTransmitBuffer(&io_pInstance->stTransmitter);

                /* send: <SN=curSN><AN=curAN><CTL=ACK><DATA> */
                _buildAckDataPacket(io_pInstance);

                io_pInstance->eRatpEventHappened = RATP_OPENED;
                io_pInstance->eState = ESTABLISHED;

                /* check if we received any data */
                if(io_pInstance->stPktParser.sHeader.data_length > 0)
                {
                    /* update the received datagram length */
                    io_pInstance->stReceiver.u16RecDatagramLen += io_pInstance->stPktParser.u16RecPayloadLen;

                    if( io_pInstance->stPktParser.sHeader.control.bits.eor == true )
                    {
                        /* This packet contained data and end of record, pass on the complete datagram to user */
                        io_pInstance->stReceiver.bReceiveComplete = true;
                    }
                }
            }
        }
    }
}

/*******************************************************************************************
 * _respondEstablished -
 *
 *  This state represents a connection fully opened at both ends.  This is the normal state
 * for data transfer.
 */
void MSrv_flatfrog_touch::_respondEstablished(RATP_instance * io_pInstance)
{
    /* C2  D2  E  F2  H2  I1 */
    /* -------------------------------- C0 -------------------------------------- */
    if(_doProcedureC0C2(io_pInstance, true))
    {
        /* ~Duplicate Packet has been handled */
    }
    else if(io_pInstance->stPktParser.sHeader.control.bits.rst)
    {   /* sn=ackNum, rst=1 */
        /* -------------------------------- D2 -------------------------------------- */
        /* flush the transmission queue (TBD), inform the user "error: connection reset" and
         * go to CLosed state
        */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
        io_pInstance->eState = CLOSED;
    }
    /* -------------------------------- E -------------------------------------- */
    else if(_doProcedureE(io_pInstance))
    {
        /* Unexpected SYN packet has been handled */
    }
    /* sn=ackNum, syn=0 */
    /* -------------------------------- F2 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
    {   /* sn=ackNum, syn=0 ,ack=0 */
        /* Check the presence of the ACK flag.  If ACK is not set then discard the packet and
           return without any further processing.
        */
        STAT(io_pInstance, rxNonAckInEst);
    }
    else
    {
        /* sn=expected, syn=0 ,ack=1 */
        /* If the AN field value is curSN+1 modulo 2 and SNused is true, then this is an
         * acknowledgement of our previous data. Free the retransmission, increment curSN+1 modulo 2
         * and set SNused = false.
         */
        if(io_pInstance->stPktParser.sHeader.control.bits.an != io_pInstance->u8CurSN &&
           io_pInstance->bSNUsed)
        {
            _updateRTTGood(io_pInstance);
            io_pInstance->u8CurSN ^=1;
            io_pInstance->bSNUsed = false;
            _advanceTransmitBuffer(&io_pInstance->stTransmitter);
            STAT(io_pInstance, rxWithNewAck);
        }

        /* -------------------------------- H2 -------------------------------------- */
        /* Check the presence of the FIN flag.  If FIN is not set then continue the processing
         * associated with this state.
         */
        if(io_pInstance->stPktParser.sHeader.control.bits.fin)
        {
            /* The FIN flag was set.  This means the other end has decided to close the connection.
             * Flush the retransmission queue. The user must also be informed "Connection closing."
             * An acknowledgment for the FIN must be sent which also indicates this end is closing:
             * set curSN = received AN (it already is), curAN = received SN+1 modulo2, SNused = true
             *    <SN=curSN><AN=curAN><CTL=FIN, ACK>
             */
            io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
            io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;

            _flushTransmitter(&io_pInstance->stTransmitter);
            _buildFinPacket(io_pInstance, true);

            /* go to LAST_ACK without any further processing */
            io_pInstance->eRatpEventHappened = RATP_CLOSING;
            io_pInstance->eState = LAST_ACK;
        }
        else
        {
            /* fin=0, sn=ackNum, syn=0 ,ack=0 AND ack=1 */
            /* -------------------------------- I1 -------------------------------------- */
            /* This represents that stage of processing in the ESTABLISHED state in which all the
             * flag bits have been processed and only data may remain.  The packet is examined to
             * see if it contains data. If not the packet is now discarded, return to the current
             * state without any further processing.
             */
            if(io_pInstance->stPktParser.sHeader.data_length > 0)
            {
                io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
                io_pInstance->stReceiver.u16RecDatagramLen += io_pInstance->stPktParser.u16RecPayloadLen;
                STAT(io_pInstance, rxNewData);
                if(io_pInstance->stPktParser.sHeader.control.bits.eor == true)
                {
                    /* Deliver complete datagram */
                    io_pInstance->stReceiver.bReceiveComplete = true;
                    STAT(io_pInstance, rxCompleteDatagram);
                }
            }

            /* At this moment we peek ahead in the transmit queue to see if data is available. if yes, we advance transmit buffer
             * <SN=curSN><AN=curAN><CTL=ACK>.  If data is queued to send, include it and set SNused = true.
             */
            if(_lookAheadTransmitBuffer(&io_pInstance->stTransmitter) ||
               (io_pInstance->stPktParser.sHeader.data_length > 0))
            {
                _buildAckDataPacket(io_pInstance);
            }
            else
            {
                STAT(io_pInstance, noDataAvailAtAckOnly);
                io_pInstance->stTransmitter.eState = TX_IDLE;
            }
        }
    }
}

void MSrv_flatfrog_touch::_respondClosed(RATP_instance * io_pInstance)
{
    /* G */
    /* All incoming packets are discarded. If the packet had the RST flag set or is a valid SYN <SN=0><CTL=SYN>
     * packet take no action. Otherwise it is necessary to build a RST packet. Since this end is closed the other end
     * of the connection has incorrect data about the state of the connection and should be so informed.
     */
    if(io_pInstance->stPktParser.sHeader.control.bits.rst ||
       (io_pInstance->stPktParser.sHeader.control.bits.syn &&
        (io_pInstance->stPktParser.sHeader.control.bits.fin == 0) &&
        (io_pInstance->stPktParser.sHeader.control.bits.sn == 0) &&
        (io_pInstance->stPktParser.sHeader.control.bits.ack == 0)))
    {
        /* RST or valid SYN - ignore */
    }
    else
    {
        /* rst not set nor is it a valid syn packet -  respond with a reset */
        /* If the ACK flag was set then send:
         *   <SN=received AN><CTL=RST>
         * If the ACK flag was not set then send:
         *   <SN=0><AN=received SN+1 modulo 2><CTL=RST, ACK>
         */
        _advanceTransmitBuffer(&io_pInstance->stTransmitter);
        if(io_pInstance->stPktParser.sHeader.control.bits.ack)
        {
            io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
            _buildRstPacket(io_pInstance, false);
        }
        else
        {
            io_pInstance->u8CurSN = 0;
            io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
            _buildRstPacket(io_pInstance, true);
        }
    }
}

void MSrv_flatfrog_touch::_respondFinWait(RATP_instance * io_pInstance)
{
    /* C2  D2  E  F3  H3 */
    /* -------------------------------- C2 -------------------------------------- */
    if(_doProcedureC0C2(io_pInstance, false))
    {
        /* ~Duplicate Packet has been handled */
    }
    /* -------------------------------- D2 -------------------------------------- */
    else if(io_pInstance->stPktParser.sHeader.control.bits.rst)
    {   /* sn=ackNum, rst=1 */
        /* flush the transmission queue (TBD), inform the user "error: connection reset" and
         * go to CLosed state
        */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
        io_pInstance->eState = CLOSED;
    }
    /* -------------------------------- E -------------------------------------- */
    else if(_doProcedureE(io_pInstance))
    {
        /* Unexpected SYN packet has been handled */
    }
    /* -------------------------------- F3 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
    {
        /* If ACK is not set then discard the packet and return without any further processing. */
    }
    /* -------------------------------- H3 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.fin)
    {
        /* The packet did not contain a FIN we assume this packet is a duplicate and that the other
         * end of the connection has not seen the FIN packet we sent earlier. Rely upon retransmission
         * of our earlier FIN packet to inform the other end of our desire to close.
         * Discard the packet and return without any further processing.
         */
    }
    else if(io_pInstance->stPktParser.sHeader.data_length > 0)
    {
        /* We have a FIN.  By the rules of this protocol an ACK of a FIN requires a FIN, ACK
         * in response and no data.  If the packet contains data we have detected an illegal
         * condition.  Send a reset: <SN=received AN><AN=received SN+1 modulo 2><CTL=RST, ACK>
         * and close.
         */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
        io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
        _buildRstPacket(io_pInstance, true);
        io_pInstance->eState = CLOSED;
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
    }
    else if(io_pInstance->stPktParser.sHeader.control.bits.an == !io_pInstance->u8CurSN)
    {
        /* This packet acknowledges our previously sent FIN packet. The other end of the connection
         * is then also assumed to be closing and expects an acknowledgment. Increment curSN+1 modulo 2.
         * <SN=curSN><AN=curAN><CTL=ACK>
         */
        io_pInstance->u8CurSN = io_pInstance->stPktParser.sHeader.control.bits.an;
        io_pInstance->u8CurAN = !io_pInstance->stPktParser.sHeader.control.bits.sn;
        io_pInstance->bSNUsed = false;

        _buildAckDataPacket(io_pInstance);

        io_pInstance->bSRTT = true;
        io_pInstance->u16SRTTTickCtr = 0;
        io_pInstance->eState = TIME_WAIT;
    }
    else
    {
        /* the AN field value was unexpected.  This indicates a simultaneous closing by both sides of
         * the connection.  Send an acknowledgment of the FIN:
         * <SN=curSN><AN=curAN><CTL=ACK>
         */
        _buildAckDataPacket(io_pInstance);

        /* Discard the packet, and go to the CLOSING state without any further processing. */
        io_pInstance->eState = CLOSING;
    }
}

void MSrv_flatfrog_touch::_respondLastAck(RATP_instance * io_pInstance)
{
    /* C2  D3  E  F3  H4 */
    /* -------------------------------- C2 -------------------------------------- */
    if(_doProcedureC0C2(io_pInstance, false))
    {
        /* ~Duplicate Packet has been handled */
    }
    /* -------------------------------- D3 -------------------------------------- */
    else if(io_pInstance->stPktParser.sHeader.control.bits.rst)
    {
        /* RST is now assumed to have been set.  Discard the packet, delete the TCB, and go to the
           CLOSED state without any further processing.
        */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
        io_pInstance->eState = CLOSED;
    }
    /* -------------------------------- E -------------------------------------- */
    else if(_doProcedureE(io_pInstance))
    {
        /* Unexpected SYN packet has been handled */
    }
    /* -------------------------------- F3 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
    {
        /* If ACK is not set then discard the packet and return without any further processing. */
    }
    else if(io_pInstance->stPktParser.sHeader.control.bits.an == !io_pInstance->u8CurSN)
    {
        /* -------------------------------- H4 -------------------------------------- */
        /* AN field value is as expected, this ACK is in response to the FIN, ACK packet recently sent.
         * This is the final acknowledging message indicating both side's agreement to close the connection.
         * Discard the packet, flush all the queues, delete the TCB, and go to the CLOSED state without any
         * further processing.
         */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eState = CLOSED;
        io_pInstance->eRatpEventHappened = RATP_CLOSED;
    }
    else
    {
        /* The AN field value was unexpected.  Discard the packet and remain in the current state
         * without any further processing.
         */
    }
}

void MSrv_flatfrog_touch::_respondClosing(RATP_instance * io_pInstance)
{
    /* C2  D3  E  F3  H5 */
    /* -------------------------------- C2 -------------------------------------- */
    if(_doProcedureC0C2(io_pInstance, false))
    {
        /* ~Duplicate Packet has been handled */
    }
    /* -------------------------------- D3 -------------------------------------- */
    else if(io_pInstance->stPktParser.sHeader.control.bits.rst)
    {
        /* RST is now assumed to have been set.  Discard the packet, delete the TCB, and go to the
         * CLOSED state without any further processing.
         */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eRatpEventHappened = RATP_DISCONNECTED;
        io_pInstance->eState = CLOSED;
    }
    /* -------------------------------- E -------------------------------------- */
    else if(_doProcedureE(io_pInstance))
    {
        /* Unexpected SYN packet has been handled */
    }
    /* -------------------------------- F3 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
    {
        /* If ACK is not set then discard the packet and return without any further processing. */
    }
    /* -------------------------------- H5 -------------------------------------- */
    else if(io_pInstance->stPktParser.sHeader.control.bits.an == !io_pInstance->u8CurSN)
    {
        /* The AN field value is as expected. This packet acknowledges the FIN packet recently sent.
         * This is the final acknowledging message indicating both side's agreement to close the connection.
         * Start the 2*SRTT timer associated with the TIME-WAIT state, discard the packet, and go to the TIME-WAIT
         * state without any further processing.
         */
        io_pInstance->bSRTT = true;
        io_pInstance->u16SRTTTickCtr = 0;
        io_pInstance->eState = TIME_WAIT;
    }
    else
    {
        /* AN field value was unexpected.  Discard the packet and remain in the current state without any further
         * processing.
         */
    }
}

void MSrv_flatfrog_touch::_respondTimeWait(RATP_instance * io_pInstance)
{
    /* D3  E  F3  H6 */
    /* -------------------------------- D3 -------------------------------------- */
    if(io_pInstance->stPktParser.sHeader.control.bits.rst)
    {
        /* Discard the packet, delete the TCB, and go to the CLOSED state without any further processing.
        */
        _flushTransmitter(&io_pInstance->stTransmitter);
        io_pInstance->eRatpEventHappened = RATP_CLOSED;
        io_pInstance->eState = CLOSED;
    }
    /* -------------------------------- E -------------------------------------- */
    else if(_doProcedureE(io_pInstance))
    {
        /* Unexpected SYN packet has been handled */
    }
    /* -------------------------------- F3 -------------------------------------- */
    else if(!io_pInstance->stPktParser.sHeader.control.bits.ack)
    {
        /* If ACK is not set then discard the packet and return without any further processing. */
    }
    /* -------------------------------- H6 -------------------------------------- */
    else if(io_pInstance->stPktParser.sHeader.control.bits.fin)
    {
        /* The FIN flag was set.  This situation indicates that the last acknowledgment of the FIN packet
         * sent by the other end of the connection did not arrive.  Resend the acknowledgment:
         * <SN=curSN><AN=curAN><CTL=ACK>
         */
        _flushTransmitter(&io_pInstance->stTransmitter);
        _buildAckDataPacket(io_pInstance);

        /* Restart the 2*SRTT timer, discard the packet and remain in the current state without any further processing. */
        io_pInstance->u16SRTTTickCtr = 0;
    }
    else
    {
        /* FIN is not set, discard the packet and return without any further processing. */
    }
}

/*
 *******************************************************************************************
 *                      RATP API's
 *******************************************************************************************
 */

/*******************************************************************************************
 *  Initialize the RATP interface and to open the connection.
 */
RATP_Return MSrv_flatfrog_touch::RATP_open(Private_RATP_Instance i_pInstance,
    void *i_pLLHandle,
    uint16_t i_u16TickRateMs,
    uint32_t i_u32BaudRate,
    bool (*CallbackFnSnd)(void *i_pLLHandle,const uint8_t* pu8Buf, uint16_t i_u16BufLen),
    void (*CallbackFnRecv)(void *i_pLLHandle,uint8_t* pu8Buf, uint16_t i_u16BufLen),
    void (*CallbackFnEvent)(void *i_pLLHandle,RATP_Return o_Type))
{
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;

    // error checks
    if( i_pInstance == NULL     ||  // instance memory not allocated
        i_pLLHandle == NULL     ||
        CallbackFnSnd == NULL   ||  // call back function not provided
        CallbackFnRecv == NULL  ||  // call back function not provided
        CallbackFnEvent == NULL ||  // call back function not provided
        i_u16TickRateMs == 0    ||  // tick rate and baude rate needed
        i_u32BaudRate == 0)         // to compute retry and user timeout
    {
        return RATP_ERROR;
    }

    /* initialize the instance members */
    my_inst->u8CurSN = 0;
    my_inst->u8CurAN = 1;
    my_inst->bSNUsed = false;
    my_inst->bSRTT = false;
    my_inst->u16RetryTimeoutCtr = 0;
    my_inst->u16SendFailCtr = 0;
    my_inst->u16SRTTTickCtr = 0;
    my_inst->u16UserTimeoutCtr = 0;
    my_inst->u16UserTimeoutTicks = 0;
    my_inst->pUserHandle = i_pLLHandle;
    my_inst->send_callback = CallbackFnSnd;
    my_inst->recv_callback = CallbackFnRecv;
    my_inst->event_callback = CallbackFnEvent;
#ifdef RATP_STATISTICS
    memset((uint8_t *)&my_inst->sStatistics, 0, sizeof(my_inst->sStatistics));
#endif

    /* initialize the transmitter */
    _initTransmitter(&my_inst->stTransmitter);

    /* initialize the receiver */
    _initReceiver(&my_inst->stReceiver);

    // initialize the packet parser
    my_inst->stPktParser.eState = SYNCH_DETECTION;

    // calculate the user timeout in terms of ticks
    my_inst->u16UserTimeoutTicks = (uint16_t)RATP_USER_TIMEOUT/i_u16TickRateMs;

    // calculate the retransmission timeout in ticks
    my_inst->u16q6MinRoundTripTime = 16*((uint16_t)((RATP_RETX_TIMEOUT*10*RATP_MAX_PACKET_SIZE*1000)/((double)i_u32BaudRate * i_u16TickRateMs) + 0.5));

    if(my_inst->u16q6MinRoundTripTime == 0)
    {
        return RATP_ERROR;
    }
    my_inst->u16q6RoundTripTime = RATP_DEFAULT_ROUND_TRIP_TIME_MULT * my_inst->u16q6MinRoundTripTime;

    // prepare SYN packet   <SN=0><CTL=SYN><MDL=RATP_MDL_SIZE>
    _buildSynPacket(my_inst, false);
    _trySendAckExpected(my_inst);

    /* allocate an extra response size packet to handle case where we have to send syn+ack and we already have data on tx buff */
    _allocateTransmitBuffer(&my_inst->stTransmitter,RATP_HEADER_ONLY,false);

    my_inst->eState = SYN_SENT;
    return RATP_SUCCESS;
}

/*******************************************************************************************
 * The application calls this API to send message/data to the connected party.
 */
RATP_Return MSrv_flatfrog_touch::RATP_send(Private_RATP_Instance i_pInstance, const uint8_t* i_pu8Buffer, uint16_t i_u16Length)
{
    RATP_Return eRet;
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;

    // error checks
    if( i_pInstance == NULL ||
        i_pu8Buffer == NULL ||
        i_u16Length == 0  || i_u16Length > RATP_MAX_DATAGRAM_SIZE )
    {
        return RATP_ERROR;
    }

    my_inst->eRatpEventHappened = RATP_SUCCESS;

    switch (my_inst->eState) {
    case SYN_SENT:
    case SYN_RECEIVED:
        {
            /* connection is being sent up. save the datagram and return */
            if(_pushTransmitBuffer(&my_inst->stTransmitter, i_pu8Buffer, i_u16Length))
            {
                STAT(my_inst, sendSuccess);
                eRet = RATP_SUCCESS;
            }
            else
            {
                STAT(my_inst, sendPending);
                eRet = RATP_PENDING;
            }
            break;
        }
    case ESTABLISHED:
        {
            /* connection is being sent up. save the datagram and return */
            if(_pushTransmitBuffer(&my_inst->stTransmitter, i_pu8Buffer, i_u16Length))
            {
                /* check if we should send proactively */
                if(my_inst->stTransmitter.eState == TX_IDLE)
                {
                    /* send proactively */
                    _buildAckDataPacket(my_inst);
                    _trySendAckExpected(my_inst);
                    STAT(my_inst, sendFromIdle);
                }
                STAT(my_inst, sendSuccess);
                eRet = RATP_SUCCESS;
            }
            else
            {
                STAT(my_inst, sendPending);
                eRet =  RATP_PENDING;
            }
            break;
        }
    case FIN_WAIT:
    case LAST_ACK:
    case CLOSING:
    case TIME_WAIT:
    case CLOSED:
    default:
        return RATP_ERROR;
    }
    /* send event if any */
    if(my_inst->eRatpEventHappened != RATP_SUCCESS)
    {
        my_inst->event_callback(my_inst->pUserHandle,my_inst->eRatpEventHappened);
        my_inst->eRatpEventHappened = RATP_SUCCESS;
    }
    return eRet;
}

/*******************************************************************************************
 * The application calls this API when it has received message/data from connected party over uart/tty?
 */
void MSrv_flatfrog_touch::RATP_recv(Private_RATP_Instance i_pInstance, const uint8_t* i_pu8Buffer, uint16_t i_u16Length)
{
    uint16_t u16BytesConsumed;
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;
    my_inst->eRatpEventHappened = RATP_SUCCESS;

    if( i_pInstance == NULL ||
        i_pu8Buffer == NULL ||
        i_u16Length == 0    ||
        i_u16Length > RATP_MAX_DATAGRAM_SIZE ||
        /* Ignore RATP_recv if RATP_open hasn't been performed */
        my_inst->send_callback == NULL ||
        my_inst->recv_callback == NULL ||
        my_inst->event_callback == NULL
        )
    {
        return;
    }

    while(i_u16Length)
    {
        if(_PacketReception(my_inst, &my_inst->stPktParser,i_pu8Buffer, i_u16Length,&my_inst->stReceiver.au8RecDatagram[my_inst->stReceiver.u16RecDatagramLen], &u16BytesConsumed))
        {
            switch (my_inst->eState)
            {
            case SYN_SENT:
            {
                _respondSynSent((RATP_instance *)i_pInstance);
                break;
            }
            case SYN_RECEIVED:
            {
                _respondSynReceived((RATP_instance *)i_pInstance);
                break;
            }
            case ESTABLISHED:
            {
                _respondEstablished((RATP_instance *)i_pInstance);
                break;
            }
            case FIN_WAIT:
            {
                _respondFinWait((RATP_instance *)i_pInstance);
                break;
            }
            case LAST_ACK:
            {
                _respondLastAck((RATP_instance *)i_pInstance);
                break;
            }
            case CLOSING:
            {
                _respondClosing((RATP_instance *)i_pInstance);
                break;
            }
            case TIME_WAIT:
            {
                _respondTimeWait((RATP_instance *)i_pInstance);
                break;
            }
            case CLOSED:
            {
                _respondClosed((RATP_instance *)i_pInstance);
                break;
            }
            default:
                break;
            }
        }

        /* Send any outstanding packets -- this includes send_callback */
        _Transmit(my_inst);

        /* Handle callbacks - this is just before the end of the function when everything is stable because
         *   the caller could call _open or another api during this callback
         *
         * send events if any (do this before recv_callback - only have both if just opened)
         */
        if(my_inst->eRatpEventHappened != RATP_SUCCESS)
        {
            my_inst->event_callback(my_inst->pUserHandle,my_inst->eRatpEventHappened);
            my_inst->eRatpEventHappened = RATP_SUCCESS;
        }

        /* send complete received datagram to host */
        if(my_inst->stReceiver.bReceiveComplete)
        {
            uint16_t u16rxlen = my_inst->stReceiver.u16RecDatagramLen;
            my_inst->stReceiver.bReceiveComplete = false;
            my_inst->stReceiver.u16RecDatagramLen = 0;
            my_inst->recv_callback(my_inst->pUserHandle,my_inst->stReceiver.au8RecDatagram, u16rxlen);
        }

        i_u16Length -= u16BytesConsumed;
        i_pu8Buffer += u16BytesConsumed;
    }
}

/*******************************************************************************************
* The application calls this API to query status of RATP. Ratp returns the status of its
* internal state machine.
*/
RATP_Return MSrv_flatfrog_touch::RATP_status(Private_RATP_Instance i_pInstance)
{
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;

    switch(my_inst->eState)
    {
    case SYN_SENT:
    case SYN_RECEIVED:
        return RATP_OPENING;
    case ESTABLISHED:
        return RATP_OPENED;
    case FIN_WAIT:
    case LAST_ACK:
    case CLOSING:
    case TIME_WAIT:
        return RATP_CLOSING;
    case CLOSED:
    default:
        return RATP_CLOSED;
    }
}

/*********************************************************************************************
* The application calls this API to close the connection. RATP shall then initiate connection
* closedown proceedure. Application should however continue calling _tick and _recv at least
* until the CLOSED or DISCONNECTED event is reported, at which time the application may stop _tick and
* free memory.
*/
RATP_Return MSrv_flatfrog_touch::RATP_close(Private_RATP_Instance i_pInstance)
{
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;

    if (i_pInstance == NULL)
    {
        return RATP_ERROR;
    }

    switch (my_inst->eState)
    {
    case SYN_SENT:
    case SYN_RECEIVED:
        _flushTransmitter(&my_inst->stTransmitter);
        my_inst->event_callback(my_inst->pUserHandle, RATP_CLOSED);
        my_inst->eState = CLOSED;
        break;
    case ESTABLISHED:
        _flushTransmitter(&my_inst->stTransmitter);
        if (my_inst->bSNUsed)
        {
            my_inst->u8CurSN ^= 1;
        }
        my_inst->bSNUsed = false;

        /* send fin */
        _buildFinPacket(my_inst, true);
        my_inst->eState = FIN_WAIT;
        _Transmit(my_inst);
        break;
    default:	// In all other state we will eventually timeout and close.
        break;

    }
    return RATP_SUCCESS;
}

/*******************************************************************************************
 * The application calls this approximately every u16TickRateMs milliseconds, in the same
 * thread context (or otherwise serialized) as all other APIs. Use the tick to implement
 * retransmits and timeouts.
 */
void MSrv_flatfrog_touch::RATP_tick( Private_RATP_Instance i_pInstance)
{
	
    RATP_header *pTxHdr = NULL;
    uint16_t u16SendDataSize = 0;
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;

    if( i_pInstance == NULL ||
        /* Ignore RATP_tick if RATP_open hasn't been performed */
        my_inst->send_callback == NULL ||
        my_inst->recv_callback == NULL ||
        my_inst->event_callback == NULL
        )
    {
        return;
    }

    pTxHdr = (RATP_header *)_getTransmitBuffer(&my_inst->stTransmitter, &u16SendDataSize);

    my_inst->eRatpEventHappened = RATP_SUCCESS;

    if(0 == u16SendDataSize)
    {
        /* unexpected if not in IDLE */
        my_inst->stTransmitter.eState = TX_IDLE;
    }

    switch(my_inst->stTransmitter.eState)
    {
    case TX_SEND_ACK_EXPECTED:
    {
        /* send_callback failed the last time so it hasn't been sent yet, attempt a send again */
        _trySendAckExpected(my_inst);
        STAT(my_inst, tickTryTxAgain);
    }
    break;
    case TX_SEND_NO_ACK_EXPECTED:
    {
        /* send_callback failed the last time so it hasn't been sent yet, attempt a send again */
        _trySendAckNotExpected(my_inst);
        STAT(my_inst, tickTryTxAgain);
    }
    break;
    case TX_WAIT_FOR_ACK:
    {
        STAT(my_inst, tickWaitingForAck);
        if(++my_inst->u16UserTimeoutCtr >= my_inst->u16UserTimeoutTicks)
        {
            /* user timeout, abort connection */
            _flushTransmitter(&my_inst->stTransmitter);
            my_inst->eState = CLOSED;
            my_inst->eRatpEventHappened = RATP_DISCONNECTED;
        }
        else if(++my_inst->u16RetryTimeoutCtr >= _getRetryTimeout(my_inst))
        {
            /* retransmit the tx */
            STAT(my_inst, tickRetryTimeout);
            if(my_inst->send_callback(my_inst->pUserHandle,(uint8_t *)pTxHdr,u16SendDataSize))
            {
                _updateRTTMiss(my_inst);
                _popTransmitBuffer(&my_inst->stTransmitter);
                my_inst->u16RetryTimeoutCtr = 0;
                my_inst->u16SendFailCtr = 0;
                STAT(my_inst, sendCallbackSuccess);
            }
            else if(RATP_MAX_SEND_FAIL_TICKS < my_inst->u16SendFailCtr++)
            {
                /* Failed to send many times in a row, give up */
                _flushTransmitter(&my_inst->stTransmitter);
                my_inst->eState = CLOSED;
                my_inst->eRatpEventHappened = RATP_DISCONNECTED;  // TODO - should this be RATP_ERROR?
            }
            else
            {
                STAT(my_inst, sendCallbackReject);
            }
        }
    }
    break;
    case TX_IDLE:
    default:
    {
        switch(my_inst->eState)
        {
        case SYN_SENT:
            /* If we are idle, then we aren't retrying the SYN, which means we must have sent RST.
             *   build the SYN again
             */
            _buildSynPacket(my_inst, false);
            break;
        case TIME_WAIT:
            if(++my_inst->u16SRTTTickCtr == 2)
            {
                _flushTransmitter(&my_inst->stTransmitter);
                my_inst->eState = CLOSED;
                my_inst->eRatpEventHappened = RATP_CLOSED;
            }
            break;
        default:
            break;
        }
    }
    break;
    }

    /* send events if eny */
    if(my_inst->eRatpEventHappened != RATP_SUCCESS)
    {
        my_inst->event_callback(my_inst->pUserHandle,my_inst->eRatpEventHappened);
        my_inst->eRatpEventHappened = RATP_SUCCESS;
    }
#if 0
	struct timeval tv;
    struct timezone tz;   
    struct tm *t;
     
    gettimeofday(&tv, &tz);
	t = localtime(&tv.tv_sec);
	
	HHT_LOG_DEBUG("[%d-%d-%d %d:%d:%d.%ld]--------------->Call RATP_tick function...\n",1900+t->tm_year, 1+t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec);
	LOGD("[%d-%d-%d %d:%d:%d.%ld]--------------->Call RATP_tick function...\n",1900+t->tm_year, 1+t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec);
#endif
}


#ifdef RATP_STATISTICS
/*********************************************************************************************
 * The module collects statistics from the point of _open on various internal events
 */
void RATP_Return MSrv_flatfrog_touch::RATP_getStatistics(
    Private_RATP_Instance i_pInstance, // Application supplied opaque memory
    RATP_Statistics *o_psStatistics)   // Copy of the current statistics
{
    RATP_instance *my_inst = (RATP_instance *)i_pInstance;
    *o_psStatistics = my_inst->sStatistics;
}
#endif


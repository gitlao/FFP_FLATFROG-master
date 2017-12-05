#ifndef MSRV_FLATFROG_TOUCH_H
#define MSRV_FLATFROG_TOUCH_H

/* Linux */
#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>

#include <sys/inotify.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sys/time.h>

using namespace std;

#include <cutils/sockets.h>
#include <cutils/properties.h> //固件类型

#define  HHT_DEBUG
//* A user defined struct that can contain connection specific data */
#define MAX_CONNECTION_CLIENTS  10
#define UNIX_DOMAIN_SOCKET_NAME "tvos" //
#define SOCKET_PATH 	"flatfrog_touch" //socket与服务进程同名

#define MASK_ZONE_REQUEST   "MaskZone->1"
#define MASK_ZONE_REQUEST2  "MaskZone->2"
#define MASK_ZONE_REQUEST3  "MaskZone->3"
#define MASK_ZONE_REQUEST4  "MaskZone->4"
#define MASK_ZONE_REQUEST5  "MaskZone->5"

#define SET_MASK_REQUEST   	         "SetMask->"
#define RELEASE_MASK_REQUEST    "ReleaseMask->"
#define CHANGE_MASK_REQUEST   "ChangeMask->"
#define SET_CHILDREN_LOCK            "CHILDREN_LOCK"
#define RELEASE_CHILDREN_LOCK  "CHILDREN_UNLOCK"


//X物理坐标与X逻辑坐标比例
#define XRATIO (30321/1920)
//Y物理坐标与Y逻辑坐标比例
#define YRATIO (17055/1080)

#define XRATIO2 (32767/1920)
#define YRATIO2 (32767/1080)

#define  TOUCH_MAX_X  32767   //触摸物理坐标
#define  TOUCH_MAX_Y  32767

#define  ANDROID_HRES  1920   //Android逻辑坐标
#define  ANDROID_VRES  1080
#define  MAX_DISABLE_MASK_ZONES   3 //最大屏蔽区域触摸数


#include "ffprotocal.h"

struct ffprotocol_device_options
{
    char device[256];
    unsigned int speed;
};
//区域触摸信息
typedef struct
{
    uint8_t top_left_x_low;
    uint8_t top_left_x_high;
    uint8_t top_left_y_low;
    uint8_t top_left_y_high;

    uint8_t bottom_right_x_low;
    uint8_t bottom_right_x_high;
    uint8_t bottom_right_y_low;
    uint8_t bottom_right_y_high;
}REQUEST_MASK_ZONE_INFO;

typedef struct
{
    // int  controlID;//控制ID 0x00 0x01 0x02
    bool isUsed;   //true  //false
    REQUEST_MASK_ZONE_INFO mRequset_mask_zone_info;

}MASK_ZONE_CONTROL;

//-----------------------------------------------------------------------------
/* Max/Defaults */
#define RATP_HEADER_ONLY                0           /* ratp header packet only */
#define RATP_HEADER_SIZE                4           /* bytes */
#define RATP_RETX_TIMEOUT               4           /* packet retransmit timeout count */
#define RATP_USER_TIMEOUT               5000.0      /* msecs */
#define RATP_SYNCH_LEADER               0x01        /* synch leader byte */
#define RATP_MDL_SIZE                   255         /* bytes */
#define RATP_DATA_CHECKSUM_SIZE         2
#define RATP_HDR_CS_DATA_SIZE           (RATP_HEADER_SIZE - 1)
#define RATP_DEFAULT_ROUND_TRIP_TIME_MULT 4         /* times the minimum */
#define RATP_MAX_ROUND_TRIP_TIME_MULT     8         /* times the minimum */

/* maximum size of ratp packet */
#define RATP_MAX_PACKET_SIZE            (RATP_HEADER_SIZE + RATP_MDL_SIZE + RATP_DATA_CHECKSUM_SIZE)
#define RATP_SIZEOF_MDL_TRANSMIT_BLOCK  (sizeof(TransmitBlock) + RATP_MDL_SIZE + RATP_DATA_CHECKSUM_SIZE)
/* Tx Buffer: max datagram plus headers and checksums for all the blocks, plus an outstanding block, plus nearly a block
 *    lost due to skip, plus a header used to mark the front, plus 1 so head does not run over tail.
 */
#define RATP_SIZEOF_TRANSMIT_BUFFER     (RATP_MAX_DATAGRAM_SIZE + ((RATP_MAX_DATAGRAM_SIZE+RATP_MDL_SIZE-1)/RATP_MDL_SIZE) * (sizeof(TransmitBlock)+RATP_DATA_CHECKSUM_SIZE)\
                                         + 2 * RATP_SIZEOF_MDL_TRANSMIT_BLOCK + sizeof(TransmitBlock) + 1)

/* if send fails this many times in a row, close */
#define RATP_MAX_SEND_FAIL_TICKS        10

// Undefine the following to remove the statistics collection and interface
//#define RATP_STATISTICS
#ifdef RATP_STATISTICS
typedef struct
{
    uint32_t sendCallbackSuccess;  // send callback returns true
    uint32_t sendCallbackReject;   // send callback returns false
    uint32_t rxHeaderOkay;         // header checksum succeeds
    uint32_t rxHeaderFail;         // header checksum fails
    uint32_t rxDataOkay;           // data checksum succeeds
    uint32_t rxDataFail;           // data checksum fails
    uint32_t buildDataPacket;      // create packet with data to send
    uint32_t buildAckPacket;       // create ack only packet to send
    uint32_t rxWithNewAck;         // received expected sn with a new an
    uint32_t rxDupWithNewAck;      // received a duplicate sn with new an
    uint32_t rxNewData;            // received a new data chunk
    uint32_t rxDupData;            // received duplicate data
    uint32_t rxCompleteDatagram;   // delivered a complete datagram to the app
    uint32_t rxNonAckInEst;        // received unexpected non-ack packet in established state
    uint32_t noDataAvailAtAckOnly; // after rx ack only packet, there is no data to send
    uint32_t sendSuccess;          // RATP_send succeeded
    uint32_t sendPending;          // RATP_send pended
    uint32_t sendFromIdle;         // RATP_send initiated transmit from idle state
    uint32_t tickTryTxAgain;       // sendCallbackReject occurred - trying again in _tick
    uint32_t tickWaitingForAck;    // _tick occurred while waiting for an ACK
    uint32_t tickRetryTimeout;     // _tick retried a packet
} RATP_Statistics;
#define RATP_STATISTICS_SIZE (sizeof(RATP_Statistics))
#else
#define RATP_STATISTICS_SIZE (0)
#endif

/* Max allowed Datagram size.
 * This is an arbitary number that can be changed as needed, but needs to match the peer
 */
#define RATP_MAX_DATAGRAM_SIZE  2060

/* Memory (in bytes) required to run each interface instance. The application is expected
 * to allocate this memory before calling RATP_open()
 */
#define RATP_INSTANCE_SIZE  (2*RATP_MAX_DATAGRAM_SIZE + 1000 + RATP_STATISTICS_SIZE)

/* Pointer to opaque RATP object. */
typedef void* Private_RATP_Instance;

/* return codes */
typedef enum RATP_Return
{
    RATP_ERROR,         /* incorrect input parameters */
    RATP_SUCCESS,       /* function has completed correctly */
    RATP_PENDING,       /* internal buffers are full, unable to accept this datagram. */
    RATP_OPENING,       /* ratp connection to peer is being setup. */
    RATP_OPENED,		/* ratp connection has been established with peer */
    RATP_CLOSING,		/* ratp connection with peer is being closed */
    RATP_CLOSED,        /* ratp connection closed due to a request from us or peer */
    RATP_DISCONNECTED,  /* ratp connection reset by peer or
                         * user timeout: multiple failures have caused RATP to abandon the connection */
    RATP_RETURN_INVALID,/* one beyond the maximum allowed value */
} RATP_Return;

/* A handle that can be used for the users purpose */
/* For example keep track of sent bytes etc */
typedef struct _ratpUserHandle
{
    uint8_t  SendCallBackData[261];
    uint8_t  RecvCallBackData[2060];
    uint16_t SendCallBackDataSize;
    uint16_t RecvCallBackDataSize;
    int fd;
    bool pendTx;  // if true, don't accept more send data
    RATP_Return lastEvent;
} ratpUserHandle;

/* Protocol States */
typedef enum RatpState
{
    SYN_SENT,           /* waiting for a matching connection request after having sent one */
    SYN_RECEIVED,       /* waiting for a confirming connection request acknowledgement */
    ESTABLISHED,        /* connection fully open at both ends  */
    FIN_WAIT,           /* waiting for connection termination request from other end */
    LAST_ACK,           /* we have seen and acknowledged a termination request from other end */
    CLOSING,            /* we are waiting for an acknowledgment of our connection termination request. */
    TIME_WAIT,          /* waiting for enough time to pass to be sure that the other end of the connection
                        received the acknowledgment of its termination request. */
    CLOSED,             /* we are in completely terminated connection. we will neither send nor receive data or control packets. */
    MAX_STATE
}RatpState;

/* packet reception states */
typedef enum PktReception
{
    RECEPTION_ERROR,
    HEADER_RECEPTION,   // the parser is searching for a synch_header
    SYNCH_DETECTION,    // the parser found a synch_header and is now doing header verification
    DATA_RECEPTION,     // the parser found a valid header and now receiving data and will do data verification
    PACKET_RECEIVED     // data verified, complete packet received
}PktReception;

/*
    The first byte following the SYNCH pattern is the control byte.
*/
typedef struct control_byte
{
    uint8_t so:1;   // single octet flag
    uint8_t eor:1;  // end of record
    uint8_t an:1;   // acknowledge number;
    uint8_t sn:1;   // sequence number associated with a packet
    uint8_t rst:1;  // reset flag; no data may be sent in a packet which has rst flag set.
    uint8_t fin:1;  // finishing flag; no more data will be sent nor accepted hence forth.
    uint8_t ack:1;  // acknowledge flag; data may be sent as long as syn,rst nor fin flags are set.
    uint8_t syn:1;  // synchronize flag; No dada may be sent in a packet which has syn flag set.
}control_byte;

/* Ratp packet header */
typedef struct RATP_header
{
    uint8_t         synch_leader;
    union
    {
        uint8_t         byte;
        control_byte    bits;
    }control;
    uint8_t         data_length;
    uint8_t         header_checksum;
}RATP_header;

/* transmit Block struct - */
typedef struct TransmitBlock
{
    uint16_t    len:15;  /* length of transmit packet */
    uint16_t    skip:1;  /* an indicator to wrap around the circular buffer.  */
    RATP_header RatpHdr;
}TransmitBlock;

/* Packet Parser */
typedef struct PktParser
{
    PktReception eState;        /* current state of the parser, */
    RATP_header  sHeader;      /* the received header shall be stored here */
    uint8_t u8PartHeaderLen;    /* size of data stored in Header */
    uint16_t u16RecPayloadLen;  /* length of received data. */
} PktParser;

/* transmitter states */
typedef enum TxState
{
    TX_IDLE,                    /* nothing to send */
    TX_SEND_ACK_EXPECTED,       /* we are trying to send syn,fin or ack+data, response expected */
    TX_SEND_NO_ACK_EXPECTED,    /* we are trying to send ack only packet, no response expected */
    TX_WAIT_FOR_ACK             /* we have sent something and are awaiting an ack */
}TxState;

/* Circular Buffer */
typedef struct Transmitter
{
    TxState eState;     /* transmitter states */
    uint8_t *pHead;     /* pointer to the next available empty space in the circular buffer.  */
    uint8_t *pTail;     /* pointer to the first used space in the buffer. If == pHead, buffer is empty. */
    uint8_t *pTransmit; /* pointer to the next transmit buffer to send */
    uint8_t au8Buffer[RATP_SIZEOF_TRANSMIT_BUFFER]; /* circular buff that holds send data  */
}Transmitter;

typedef struct Receiver
{
    bool bReceiveComplete;      /* A complete datagram is ready for delivery */
    uint16_t u16RecDatagramLen; /* length of received datagram */
    uint8_t au8RecDatagram[RATP_MAX_DATAGRAM_SIZE+RATP_DATA_CHECKSUM_SIZE + RATP_MAX_PACKET_SIZE];  /* buffer that holds received data */
}Receiver;

/* Application supplied RATP_INSTANCE_SIZE bytes of memory */
typedef struct RATP_instance
{
    RatpState eState;           /* ratp state machine */
    bool bSRTT;                 /* Smoothed Round Trip Time Timer- start/stop */
    PktParser stPktParser;      /* received ratp packet parsing. */
    void *  pUserHandle;        /* lower level interface handle. */
    uint8_t u8CurSN;            /* Sequence number to use for the next transmit packet. If SNUsed is true, it is the SN of outstanding data */
    uint8_t u8CurAN;            /* Ack number to use for the next transmit packet, which is SN of last correctly received data */
    bool    bSNUsed;            /* True iff data has been sent using current value of u8CurSN and is not yet acked. */
    uint16_t u16RetryTimeoutCtr;    /* tick counter for retries */
    uint16_t u16q6MinRoundTripTime; /* retransmission timeout is 4 times max packet transmit time */
    uint16_t u16q6RoundTripTime;    /* Estimate of round trip time in ticks * 64 */
    uint16_t u16SendFailCtr;        /* counts each tick send fails */
    uint16_t u16SRTTTickCtr;        /* TIME WAIT counter */
    uint16_t u16UserTimeoutCtr;     /* count towards user timeout */
    uint16_t u16UserTimeoutTicks;   /* length of user time out fixed at 5 sec */
    RATP_Return eRatpEventHappened;  /* an indicator to notify if an event occured */
    Receiver stReceiver;
    Transmitter stTransmitter;
    bool (*send_callback)(void *, const uint8_t*, uint16_t);  /* callback function to send data to lower level send function */
    void (*recv_callback)(void *,uint8_t*, uint16_t);   /* callback function to send received data to application */
    void (*event_callback)(void *,RATP_Return );    /* callback function to send events to application */
#ifdef RATP_STATISTICS
    RATP_Statistics sStatistics;
#endif
}RATP_instance;

#ifdef RATP_STATISTICS
#define STAT(inst, st) do{inst->sStatistics.st++;}while(0)
#else
#define STAT(inst, st) do{}while(0)
#endif

//-----------------------------------------------------------------------------
class MSrv_flatfrog_touch /*:public MSrv*/
{
    /* FLATFROG_TOUCH接口*/
public:
    static MSrv_flatfrog_touch* GetInstance();
    static void DestoryInstance();

    //--------------------------------uinx domain socket-------------------------------
    int  create_unix_domain_socket_conn_service();//创建unix domain socket 服务
    static void *client_conn_handler(void *arg);         //客户端链接请求处理

    //--------------------------------RATP ops------------------------------------------
    static bool SendCallback(void *i_pUserHandle, const uint8_t* pu8Buf, uint16_t u16BufLen);
    static void RecvCallback(void *i_pUserHandle,uint8_t* pu8Buf, uint16_t u16BufLen);
    static void EvtCallback(void *i_pUserHandle,RATP_Return o_Type);

    int  get_ratp_tick_rate(unsigned int baud_rate);
    void  ratp_tick_handler(clock_t *before);

    unsigned int ratp_check_incoming(uint8_t *buf, unsigned int len);
    void  handle_incoming_data(uint8_t *buf, unsigned int len);

    //------------------------------FFPROTOCAL ops--------------------------------------
    //操作控制指令集合
    void ffprotocol_query();//查询握手指令
    static void ffprotocol_static_query();
    void ffprotocol_ftouch_uart_enable();//触摸使能输出指令
    void ffprotocol_ftouch_uart_disable();
    void ffprotocol_btouch_uart_enable();//BTOUCH使能  uart
    void ffprotocol_btouch_uart_disable();
    void ffprotocol_btouch_usb_enable();//BTOUCH使能 usb
    void ffprotocol_btouch_usb_disable();

	//查询触摸框传感器尺寸
	void  ffpprotocol_ftouch_sensor_size_get();

    void disable_all_usb_touch_cmd();//禁止整个USB触摸 HTOUCH
    void enable_all_usb_touch_cmd();//使能整个USB触摸 HTOUCH

    //------------------------------FTOUCH func----------------------------------------
    Report_touch_information  *get_touch_interaction_info(uint8_t *pBuf);
    Report_touch_information  *get_passive_pen_interaction_info(uint8_t *pBuf);
    Report_touch_information  *get_large_object_interaction_info(uint8_t *pBuf);
    Report_touch_information  *get_active_pen_interaction_info(uint8_t *pBuf);
    Report_touch_information  *get_soft_key_interaction_info(uint8_t *pBuf);
    //根据截取的字节数组决定调用哪种方式
    void get_report_info_ctl(uint8_t *pBuf,bool* downflag);

    //-------------------------------mask zone----------------------------------------
    int set_usb_mask_zone_control_rect(int controlID,REQUEST_MASK_ZONE_INFO rect);
    int request_disable_usb_mask_zone(REQUEST_MASK_ZONE_INFO rect);
    int release_usb_mask_zone_control(int controlID);
    int request_change_disable_usb_mask_zone(int controlID,REQUEST_MASK_ZONE_INFO rect);
    REQUEST_MASK_ZONE_INFO get_request_mask_zone_info(char* rvbuf);
    int get_request_type(const char *rvbuf);

    /**
     * //特定区域对应特定ID :0->FlatBar 1->WhiteBoard 2->QuickSetting
     * //SetMask->id|0,0,0,0|0,0,0,0     (SetMask->1|0,0,0,0)
     * //ChangeMask->id|0,0,0,0|0,0,0,0  (ChangeMask->id|0,0,0,0)
     * //ReleaseMask->id|0,0,0,0|0,0,0,0 (ReleaseMask->1|0,0,0,0)
     */
    void set_mask_zone_with_id(int id,char *coordinate);
	void release_flatbar_mak_zone(int id,uint8_t *location);//
    void release_mask_zone_with_id(int id,char*coordinate);
    void change_mask_zone_with_id(int id,char *coordinate);
	void set_all_mask_zones_with_one_id(int id,char *coordinate);//一个id设置所有区域触摸
    int  get_static_request_type(const char*rvbuf);
    //出现连接断开时清除所有设置
    void clear_all_mask_zones();
    //--------------------------------help func-----------------------------------------
    int wakeup_system_when_sleep();
    void split(char **arr, char *str, const char *delims);
    static char*convert_hex_to_str(unsigned char *pBuf, const int nLen);
    void sleep_ms(unsigned int msec);

    //-----------------------------virual input devices----------------------------------
    // 创建虚拟输入设备
    int create_virtual_input_device(void);
#if 1
    void send_mt_abs_touch_key_down_event(int pos_id, int xpos, int ypos);//按键
    void send_mt_abs_touch_key_up_event(int pos_id, int xpos, int ypos);
    void send_mt_abs_touch_figner_down_event(int pos_id,int xpos,int ypos);//手指
    void send_mt_abs_touch_figner_up_event(int pos_id,int xpos,int ypos);
    void send_mt_abs_touch_pen_down_event(int pos_id,int xpos,int ypos);//笔
    void send_mt_abs_touch_pen_up_event(int pos_id,int xpos,int ypos);

	void send_mt_abs_stylus_down_event(int pos_id,int xpos,int ypos);//STYLUS
    void send_mt_abs_stylus_up_event(int pos_id,int xpos,int ypos);
	
    void send_mt_abs_touch_rubber_down_event(int pos_id,int xpos,int ypos);//橡皮擦
    void send_mt_abs_touch_rubber_up_event(int pos_id,int xpos,int ypos);

    void send_mt_abs_event(int pos_id, int abs_x, int abs_y);
    int report_key_event(int fd,unsigned short code,int pressed,struct timeval*time);
    int report_rel_event(int fd, unsigned short code, int value, struct timeval *time);
    int report_abs_event(int fd, unsigned short code, int value, struct timeval *time);
    int report_sync_event(int fd, int code, struct timeval *time);
#endif

    void set_receive_data(string rvbuf);
    string get_receive_data();
	void set_receive_type(int type);
	int get_receive_type();
	
    void start();//启动该服务
      

//-----------------------------------------------------------------------------------------------
/* 串口接口*/
public:
    static int open_serial_port(char * device, unsigned int speed);

    static ssize_t serial_write(unsigned char *buf, unsigned int len);

    static int serial_read(unsigned char *buf, unsigned int len);

    static void close_serial_port(int fd);

    static int set_interface_attribs (int fd, speed_t speed, int parity);

    static void set_blocking (int fd, int should_block);

    static speed_t baud_to_code(unsigned int speed);

    static void serial_tcsetattr(void);

    static void wait_for_char(unsigned char *c);

    static void serial_clear_screen(void);
//-----------------------------------------------------------------------------------------------
/* RATP接口*/
public:
    static uint8_t _HeaderChecksum(const RATP_header* pHeader);
    static uint16_t _Checksum16(const uint8_t* pu8Buf, uint16_t i_u16BufLen);
    static void _initTransmitter(Transmitter *o_pTransmitter);
    static void _flushTransmitter(Transmitter *o_pTransmitter);
    static void _initReceiver(Receiver *o_pReceiver);
    static uint8_t * _allocateTransmitBuffer(Transmitter *i_pTransmitter, uint8_t i_DataSize, bool bEndOfRecord);
    static uint8_t * _getTransmitBuffer(Transmitter *i_pTransmitter, uint16_t *o_pu16Length);
    static void _BuildTransmitPacket(uint8_t *io_pHead, const uint8_t * i_pu8DataBuf, uint8_t i_u16DataSize);
    static bool _pushTransmitBuffer(Transmitter *i_pTransmitter,const uint8_t *i_pu8DataBuf, uint16_t i_u16BufLen);
    static void _popTransmitBuffer(Transmitter *io_pTransmitter);
    static void _advanceTransmitBuffer(Transmitter *io_pTransmitter);
    static bool _lookAheadTransmitBuffer(Transmitter *io_pTransmitter);
    static void _updateRTTGood(RATP_instance * io_pInstance);
    static void _updateRTTMiss(RATP_instance * io_pInstance);
    static uint16_t _getRetryTimeout(RATP_instance * io_pInstance);
    static bool _trySendAckExpected(RATP_instance * io_pInstance);
    static bool _trySendAckNotExpected(RATP_instance *io_pInstance);
    static void _Transmit(RATP_instance * io_pInstance);
    static bool _PacketReception(             // TRUE: a complete packet is available. False: waiting for more bytes
        RATP_instance * io_pInstance,  // Instance for stats
        PktParser *io_pstPktParse,     // Parser state
        const uint8_t* i_pu8InBuf,     // input bytes from the wire
        uint16_t i_u16BufLen,          // number of bytes from the wire
        uint8_t* o_pu8PayloadBuf,      // Buffer to collect payload into, should be the same until a complete packet
                                       //   is received. Space for RATP_MDL_SIZE + RATP_DATA_CHECKSUM_SIZE. Must be
                                       //   contiguous with previously received payload chunks if present.
        uint16_t *o_pu16BytesConsumed);// Number of bytes of input consumed

    static void _buildAckDataPacket(RATP_instance * io_pInstance);
    static void _buildSynPacket(RATP_instance * io_pInstance, bool i_withAck);
    static void _buildFinPacket(RATP_instance * io_pInstance, bool i_withAck);
    static void _buildRstPacket(RATP_instance * io_pInstance, bool i_withAck);
    static bool _doProcedureC0C2(RATP_instance * io_pInstance, bool i_bDoC0);
    static bool _doProcedureE(RATP_instance * io_pInstance);
    static void _respondSynSent(RATP_instance * io_pInstance);
    static void _respondSynReceived(RATP_instance * io_pInstance);
    static void _respondEstablished(RATP_instance * io_pInstance);
    static void _respondClosed(RATP_instance * io_pInstance);
    static void _respondFinWait(RATP_instance * io_pInstance);
    static void _respondLastAck(RATP_instance * io_pInstance);
    static void _respondClosing(RATP_instance * io_pInstance);
    static void _respondTimeWait(RATP_instance * io_pInstance);


    /*********************************************************************************************
     * The application must call RATP_open once before any other API.
     * It may call it again after a RATP_CLOSED or RATP_DISCONNECTED event.
     *
     * RATP does not provide thread-safety internally, so all API calls must be from the same
     * context or protected by mutexes. Callbacks may occur from any API call.
     *
     * This will initialize the interface, clear all queues and open the connection to the peer.
     * Low level transport drivers must be initialized before this.
     *
     * Only one connection is supported per lower level transport channel.
     *
     * RATP_send() and CallbackFnRecv form the upper layer data interface. Complete messages
     * (datagrams) are passed reliably between peers.
     *
     * CallbackFnSnd and RATP_recv() form the lower  layer data interface. Byte streams are
     * passed unreliably over the UART or other link.
     *
     * The application shall provide Callback functions:
     *   CallbackFnSend to send data buffer to lower level transport functions such as UART/TTY,
     *   CallbackFnRecv to send datagrams received on lower level transport to the application.
     *   CallbackFnEvent to handle changes in the link state (opening, closing)
     *
     * The application may begin sending data immediately after open, without waiting for the
     * OPENED event.
     */
    static RATP_Return RATP_open(           // RATP_ERROR if parameters are incorrect, RATP_SUCCESS otherwise
        Private_RATP_Instance o_pInstance, // Application supplied RATP_INSTANCE_SIZE bytes of memory
                                           // for the instance. This memory is Private to RATP. Must be
                                           // aligned on 32 bit boundary.
        void *i_pUserHandle,      // opaque pointer that RATP will pass to every callback function
        uint16_t i_u16TickRateMs,   // The timer tick rate in msec.
        uint32_t i_u32BaudRate,     // Baud rate of the connection

                                  // RATP calls this function to send packets over the UART or other serial line
        bool (*CallbackFnSend)(   // TRUE: Function takes ownership of i_pu8TxBuffer until the next
                                  // link time it returns TRUE. Thus it can use the buffer directly
                                  // without copying it, returning FALSE until the byte stream completes.
                                  // FALSE: Lower level is busy or not ready, function retains ownership
                                  // of previous buffer and does not take ownership of new buffer or send
                                  // its content.
            void *i_pUserHandle,     // value that was passed into RATP_open.
            const uint8_t *i_pu8TxBuffer,   // pointer to data to send
            uint16_t i_u16Length),          // number of bytes to send

        void (*CallbackFnRecv)(             // A callback function to provide received datagram.
            void *i_pUserHandle,     // value that was passed into RATP_open.
            uint8_t *i_pu8Datagram,         // pointer to datagram received
            uint16_t i_u16DatagramLength),  // length of datagram in bytes

        void (*CallbackFnEvent)(        // RATP calls this function when the link opens, closes or is restarted.
            void *i_pUserHandle,     // value that was passed into RATP_open.
            RATP_Return o_Type));       // RATP_CLOSED / RATP_DISCONNECTED

    /*********************************************************************************************
     * The application must call this approximately every u16TickRateMs milliseconds, in the same
     * thread context (or otherwise serialized) as all other APIs.
     * RATP uses the tick to implement retransmits and timeouts.
     *
     * Recommended range for u16TickRateMs:
     *            Baud          tick rate (range)
     *            9600            544 - 1080 ms
     *           19200            272 - 543 ms
     *           38400            136 - 271 ms
     *           57600             90 - 180 ms
     *          115200             45 - 90 ms
     *          230400             23 - 44 ms
     *          460800             11 - 22 ms
     *          921600              5 - 10 ms
     */
    static void RATP_tick(
        Private_RATP_Instance i_pInstance); // Application supplied RATP_INSTANCE_SIZE bytes of memory
                                            //  for the instance. This memory is Private to RATP.

    /*********************************************************************************************
     * The application calls this API to send datagram to the connected party.
     *
     * As long as there is space on the internal buffer to accomodate the new datagram, RATP will
     * copy the datagram, chunk it, send it over the serial line and retry as neccessary until the
     * peer acknowledges. The peer will receive the complete reassembled datagram in a single call
     * to its CallbackFnRecv.
     *
     * When there is not enough space, the datagram is not sent and RATP_PENDING is returned in
     * which case the application should wait and retry later (possibly after a CallbackFnSend).
     */
    static RATP_Return RATP_send (      // RATP_ERROR: parameters are incorrect
                                 // RATP_SUCCESS: datagram is accepted
                                 // RATP_PENDING: buffer is full, datagram not accepted
        Private_RATP_Instance io_pInstance, // Application supplied RATP_INSTANCE_SIZE bytes of
                                            // memory for the instance. This memory is Private to
                                            // RATP.
        const uint8_t* i_pu8Datagram,       // Pointer to datagram buffer
        uint16_t i_u16Length );             // Length of the datagram. Must be <= RATP_MAX_DATAGRAM_SIZE.

    /*********************************************************************************************
     * The application calls this API when it receives any data byte/s from peer over the low level
     * interface such as uart/tty. RATP will assemble and reconstruct the complete datagram.
     * When the complete data datagram is received, RATP shall call the CallbackFnRecv function to
     * pass the received data to the application. The Application is expected to copy this data.
     *
     * Note that RATP may call CallbackFnSend in this context to acknowledge received packets and
     * send buffered data.
     */
    static void RATP_recv (
        Private_RATP_Instance io_pInstance, // Application supplied RATP_INSTANCE_SIZE bytes of memory
                                            //  for the instance. This memory is Private to RATP.
        const uint8_t* i_pu8Buffer,         // Pointer to received data buffer
        uint16_t i_u16Length);              // Length of the received buffer.
    /*********************************************************************************************
     * The application calls this API to query status of RATP. Ratp returns the status of its
     * internal state machine.
     */
    static RATP_Return RATP_status( // any of RATP_OPENING/RATP_OPENED/RATP_CLOSING/RATP_CLOSED/RATP_DISCONNECTED
        Private_RATP_Instance i_pInstance); // Application supplied RATP_INSTANCE_SIZE bytes of memory
                                            //  for the instance. This memory is Private to RATP.

    /*********************************************************************************************
     * The application calls this API to close the connection. RATP shall then initiate connection
     * closedown proceedure. Application should however continue calling _tick and _recv at least
     * until the DISCONNECTED event is reported, at which time the application may stop _tick and
     * free memory.
     */
    static RATP_Return RATP_close(    // RATP_ERROR if parameters are incorrect, RATP_SUCCESS otherwise
                           Private_RATP_Instance i_pInstance); // Application supplied RATP_INSTANCE_SIZE bytes of memory
                                                                   //  for the instance. This memory is Private to RATP.

#ifdef RATP_STATISTICS
    /*********************************************************************************************
 * The module collects statistics from the point of _open on various internal events
 */
    static void RATP_getStatistics(
            Private_RATP_Instance i_pInstance, // Application supplied opaque memory
            RATP_Statistics *o_psStatistics);  // Copy of the current statistics
#endif

private:
	 MSrv_flatfrog_touch();
    ~MSrv_flatfrog_touch();
    static MSrv_flatfrog_touch *m_pInstance;
	
	//创建unix domain socket 服务线程
	static void*create_domain_socket_service_pthread(void*arg);
	//创建与Android连接通讯线程
	static void*create_deal_data_from_android_pthread(void*arg);
	
	static void*Run(void*arg);

	clock_t before;
	
	int client_socket ;
	
	string m_request_data;

	int m_request_type;
	
};

#endif // MSRV_FLATFROG_TOUCH_H

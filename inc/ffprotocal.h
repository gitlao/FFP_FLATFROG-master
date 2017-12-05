#ifndef FFPROTOCAL
#define FFPROTOCAL

#include <stdio.h>
#include <getopt.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <linux/limits.h>
#include <sys/poll.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <stddef.h>
#include <pthread.h>

/*max support points*/
#define  UART_TOUCH_POINT_MAX  10
/*max support pen counts*/
#define  UART_SUPPORT_PEN_MAX  10

/*
 * Command codes
 * FFP_CTRL – Miscellaneous system status and control
 * 7.6-FFP_CTRL – Miscellaneous system status and control
 */
#define FFP_CTRL_PROTOCOL_QUERY             0xe0
#define FFP_CTRL_POWER_WOT                  0xe1
#define FFP_CTRL_POWER_ACTIVE               0xe2
#define FFP_CTRL_POWER_LEAVE_WOT            0xe3
#define FFP_CTRL_FWDL_PREPARE               0xe4
#define FFP_CTRL_FWDL_UPGRADE               0xe5
#define FFP_CTRL_SEND_DATA                  0xe6
#define FFP_CTRL_RESTART                    0xe7
#define FFP_CTRL_SESSION_TERMINATE          0xe8
#define FFP_CTRL_GET_SYSERROR               0xe9
#define FFP_CTRL_POWER_SLEEP                0xea

/*
 * Status reply "cmds"
 */
#define FFP_STATUS                          0xf0
#define FFP_STATUS_DATA                     0xf1

/*
 * FFP_TOUCH - full touch
 * 7.2-FFP_FTOUCH – Full touch
 */
#define  FFP_FTOUCH_ENABLE                  0x40
#define  FFP_FTOUCH_DISABLE                 0x41
#define  FFP_FTOUCH_SENSOR_SIZE_GET         0x42
#define  FFP_FTOUCH_MAX_COUNT_GET           0x43
#define  FFP_FTOUCH_EVENT                   0x44
/*
 * FFP_TOUCH - basic touch
 * 7.3-FFP_FTOUCH – Basic touch
 */
#define FFP_BTOUCH_ENABLE                   0x20
#define FFP_BTOUCH_DISABLE                  0x21
#define FFP_BTOUCH_STICKY                   0x22
#define FFP_BTOUCH_FILTER                   0x23
#define FFP_BTOUCH_EVENT                    0x24
#define FFP_BTOUCH_SOFTKEY_STICKY           0x25
#define FFP_BTOUCH_SOFTKEY_FILTER           0x26
#define FFP_BTOUCH_SOFTKEY_EVENT            0x27

/*
 * FFP_TOUCH – General touch control
 * 7.4-FFP_FTOUCH – General touch control
 */
#define FFP_TOUCH_OFFSET                    0x30
#define FFP_TOUCH_LOGICAL_DISPLAY           0x31
#define FFP_TOUCH_MASK_ZONES                0x32
#define FPP_TOUCH_CLEAR_MASK_ZONE           0x33
#define FFP_HTOUCH_ENABLE                   0x10
#define FFP_HTOUCH_DISABLE                  0x11
/*
 * FFP_APEN – Active pen
 * 7.5-FFP_FTOUCH – Active pen
 */
#define FPP_APEN_ENUMERATE                  0x50
#define FFP_APEN_NAME_SET                   0x51
#define FFP_APEN_NAME_GET                   0x52
#define FFP_APEN_BATTERY                    0x53
#define FFP_APEN_RSSI                       0x54
#define FFP_APEN_PEN_FIRMWARE_VERSION       0x55
#define FFP_APEN_PAIRING_INFO               0x56
#define FFP_APEN_RECEIVER_FIRMWARE_VERSION  0x57
#define FFP_APEN_EVENT_DATA_SET             0x58
#define FFP_APEN_EVENT_DATA_GET             0x59
#define FFP_APEN_CFG_BTN_SET                0x5a
#define FFP_APEN_CFG_BTN_RESET              0x5c
#define FFP_APEN_CFG_BTN_REMOVE             0x5d
#define FFP_APEN_CFG_BTN_GET                0x5b


/*
 * Status codes
 */

#define FFP_STATUS_OK       (0x00)
#define FFP_STATUS_ENOENT   (0x02) /* No such file or directory */
#define FFP_STATUS_EIO      (0x05) /* I/O error                 */
#define FFP_STATUS_EAGAIN   (0x0B) /* Busy, try later           */
#define FFP_STATUS_EACCES   (0x0D) /* Permission denied         */
#define FFP_STATUS_EINVAL   (0x16) /* Invalid argument          */
#define FFP_STATUS_EFBIG    (0x1B) /* File to big               */
#define FFP_STATUS_ENOSPC   (0x1C) /* No space left on device   */
#define FFP_STATUS_ENOSYS   (0x26) /* Not implemented           */
#define FFP_STATUS_EPROTO   (0x47) /* Protocol error            */
#define FFP_STATUS_ENOTCONN (0x6B) /* Transport endpoint is not connected */
#define FFP_STATUS_UNKNOWN  (255)

//mfg id
#define FFP_MFG_ID_LENGTH   28

struct __attribute__((packed)) ffp_status
{
    uint8_t status;
    uint8_t status_code;
    uint8_t command;
};

struct __attribute__((packed)) ffp_status_data
{
    uint8_t status;
    uint8_t status_code;
    uint8_t command;
    uint8_t data_length;
    uint8_t data[];
};

//========================struct defines=====================================
struct __attribute__((packed)) ffp_data_query
{
    /* Protocol level */
    uint8_t api_hi1;
    uint8_t api_hi2;
    uint8_t api_lo1;
    uint8_t api_lo2;

    /* SW revision */
    uint8_t sw_revision_hi1;
    uint8_t sw_revision_hi2;
    uint8_t sw_revision_lo1;
    uint8_t sw_revision_lo2;

    /* TC Serial number */
    char serial_number[10];

    /*
     * Manufacturer ID
     *
     * Subtract two bytes to accomodate hw_revision at the end
     * w/o affecting the query length
     */
    uint8_t mfg_id[FFP_MFG_ID_LENGTH - 2];

    /* HW revision */
    uint8_t hw_revision_hi;
    uint8_t hw_revision_lo;
};

struct __attribute__((packed)) ffp_protocol_query_cmd
{
    uint8_t command;
    uint8_t query_length;
    uint8_t reserved1;
    uint8_t reserved2;
    struct ffp_data_query query;
};
//===========================================================================

struct __attribute__((packed)) ffp_protocol_ftouch_enable//uart 数据输出
{
       uint8_t  status;
       uint8_t  statue_code;
       uint8_t  commond;
};
struct __attribute__((packed)) ffp_protocol_ftouch_enable_cmd //请求uart输出使能指令
{
    uint8_t commond;
    uint8_t interface;
};

typedef enum //交互类型
{
    TOUCH=0x01,
    PASSIVE_PEN=0x02,
    LARGE_OBJECT=0x03,
    ACTIVE_PEN=0x04,
    SOFT_KEY=0x05,
    UNKNOW=0x06,
}INTERACTION_TYPE;

typedef enum
{
    STATUS_UP=0x00,//触摸抬起
    STATUS_DOWN=0x01,//触摸压下
    STATUS_MOVE=0x02,//移动
    STATUS_HOVER=0x03,//无任何触摸,悬空状态
    STATUS_OTHERS=0x04,//其他未定义交互状态
}INTERACTION_STATUS;//交互状态

typedef struct __attribute__((packed))_REPORT_TOUCH_INFORMATION//上报触摸信息
{
#if 0
    uint8_t       interaction_type;//交互类型
    uint8_t       modifiers;       //down, up ,[0x00-> 0x01-> 0x02->]
#else
    INTERACTION_TYPE   interaction_type;
    INTERACTION_STATUS modifiers;
#endif
    uint8_t       contact_id;//触摸id
    unsigned int  interaction_x;
    unsigned int  interaction_y;

}Report_touch_information;

typedef struct __attribute__((packed))_FFP_FTOUCH_EVENT_INTERACTION_TOUCH
{
    uint8_t interaction_type;
    uint8_t interaction_data_len;

    uint8_t contact_id;
    uint8_t modifiers;
    uint8_t x_low;
    uint8_t x_high;
    uint8_t y_low;
    uint8_t y_high;
}ffp_ftouch_event_interaction_touch;

typedef struct __attribute__((packed))_FFP_FTOUCH_EVENT_INTERACTION_PASSIVE_PEN
{
    uint8_t interaction_type;
    uint8_t interaction_data_len;

    uint8_t contact_id;
    uint8_t pen_type;
    uint8_t modifiers;
    uint8_t x_low;
    uint8_t x_high;
    uint8_t y_low;
    uint8_t y_high;
}ffp_ftouch_event_interaction_passive_pen;

typedef struct __attribute__((packed))_FFP_FTOUCH_EVENT_INTERACTION_LARGE_OBJECT
{
    uint8_t interaction_type;
    uint8_t interaction_data_len;

    uint8_t contact_id;
    uint8_t modifiers;//Modifier bit field
    uint8_t x_low;
    uint8_t x_high;
    uint8_t y_low;//Logical value of the Y coordinate
    uint8_t y_high;
    uint8_t major;//Major axis (mm)
    uint8_t minor;//Minor axis (mm)
    uint8_t azimuth;
}ffp_ftouch_event_interaction_large_object;

typedef struct __attribute__((packed))_FFP_FTOUCH_EVENT_INTERACTION_ACTIVE_PEN
{
    uint8_t interaction_type;
    uint8_t interaction_data_len;

    //Globally unique identifier of a pen, low byte first
    uint8_t serial_lo1;
    uint8_t serial_lo2;
    uint8_t serial_hi1;
    uint8_t serial_hi2;
    uint8_t modifiers;//Modifier bit field
    uint8_t x_low;
    uint8_t x_high;//
    uint8_t y_low;
    uint8_t y_high;//Range [0, Logical size Y]
    //Pressure of the tip pressure sensor Range [0, 1023]
    uint8_t pressure_low;
    uint8_t pressure_high;
    uint8_t event_data;
}ffp_ftouch_event_interaction_active_pen;

typedef struct __attribute__((packed))_FFP_FTOUCH_EVENT_INTERACTION_SOFT_KEY
{
    uint8_t usage_page;
    uint8_t modifier;
    uint8_t usage;
}ffp_ftouch_event__interaction_soft_key;

typedef struct __attribute__((packed))_FFP_FTOUCH_SENSOR_SIZE_GET
{
    uint8_t status_with_data;
    uint8_t command;
    uint8_t data_length;
    uint8_t logical_x_low;
    uint8_t logical_x_high;
    uint8_t physical_x_low;
    uint8_t physical_x_high;
    uint8_t logical_y_low;
    uint8_t logical_y_high;
    uint8_t physical_y_low;
    uint8_t physical_y_high;
}ffp_ftouch_sensor_size_get;

typedef struct __attribute__((packed))_FFP_FTOUCH_MAX_COUNT_GET
{
    uint8_t status_with_data;
    uint8_t status_code;
    uint8_t command;
    uint8_t data_length;
    uint8_t max_count;
}ffp_ftouch_sensor_max_count_get;

//----------------------------BTOCUH TYPE----------------------------------------
typedef enum //交互类型
{
    BTOUCH_TOUCH_DOWN=0x00,
    BTOUCH_TOUCH_UP =0x01,
    BTOUCH_TOUCH_ACTIVE=0x02,
    BTOUCH_TYPE_FINGER=0x03,
    BTOUCH_TYPE_PEN=0x04,
    BTOUCH_TYPE_ERASER=0x05,
    BTOUCH_RESERVED=0x06,

}INTERACTION_BTOUCH_STATUS;

typedef struct __attribute__((packed))_FFP_BTOUCH_INFORMATION//btouch上报触摸信息
{
    INTERACTION_BTOUCH_STATUS btouch_status;
    uint8_t       btouch_id;
    uint8_t       btouch_age;
    uint8_t       x_low;
    uint8_t       x_high;
    uint8_t       y_low;
    uint8_t       y_high;
}ffp_btouch_information;

typedef struct __attribute__((packed))_REPORT_BTOUCH_INFORMATION//btouch上报触摸信息
{
    INTERACTION_BTOUCH_STATUS btouch_status;
    uint8_t       btouch_id   ;
    uint8_t       btouch_age  ;
    unsigned int  coordiante_x;
    unsigned int  coordinate_y;
}Report_btouch_information;

#endif // FFPROTOCAL


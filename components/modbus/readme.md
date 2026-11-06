## modbus
目前modbus只适配了tcp从机模式，ascii和rtu模式以及tcp主机模式没有适配。
### 1. 文件结构
modbus/
|--modbus/
|     |--ascii       /ascii模式相关的代码
|     |--functions   /modbus不同状态码相关实现
|     |--include     /头文件和modbus配置文件
|     |--rtu         /rtu模式相关的代码
|     |--tcp         /tcp模式相关的代码
|     |--mb.c        /modbus协议栈
|--port/ 需要适配的相关文件
|     |--port.h       
|     |--portevent.c /适配邮箱
|     |--portother.c /debug
|     |--porttcp.c   /适配lwip tcp
|     |--user_mb.c   /在tcp从机模式下，对于不同的状态码提供不同的服务数据

### 2. 接口函数
##### 2.1 mb.c文件中的接口
mb.c文件中的接口是协议栈的核心接口，在tcp模式下，这些接口会调用mbtcp.c、portevent.c、functions目录文件下的接口。

```javascript
eMBErrorCode eMBTCPInit( USHORT ucTCPPort )
```
**描述:**
* 初始化TCP相关的配置，监听ucTCPPort端口。初始化tcp邮箱，给协议栈的读写等接口赋值。


```javascript
eMBErrorCode eMBEnable( void )
```
**描述:**
* 使能协议栈。


```javascript
eMBErrorCode eMBPoll( void )
```
**描述:**
* 协议栈的处理线程。


```javascript
eMBErrorCode eMBDisable( void )
```
**描述:**
* 去能协议栈。


```javascript
eMBErrorCode eMBClose( void )
```
**描述:**
* 关闭协议栈。
  

* ##### 2.2 mbtcp.c文件中的接口
mbtcp.c主要调用porttcp.c文件下的接口完成tcp的初始化和tcp读写操作

```javascript
eMBErrorCode eMBTCPDoInit( USHORT ucTCPPort )
void eMBTCPStart( void )
void eMBTCPStop( void )
```
**描述:**
* tcp初始化、start和stop

```javascript
eMBErrorCode eMBTCPReceive( UCHAR * pucRcvAddress, UCHAR ** ppucFrame, USHORT * pusLength )
```
**描述:**
* tcp的接收函数

```javascript
eMBErrorCode eMBTCPSend( UCHAR _unused, const UCHAR * pucFrame, USHORT usLength )
```
**描述:**
* tcp的发送函数


* ##### 2.3 functions目录下的接口
functions目录下的各个文件在的接口主要完成modbus不同状态码下的发送和接收。这些接口都注册到mb.c文件下的xMBFunctionHandler xFuncHandlers[]这个数组中，eMBPoll()函数会调用这些接口来完成对不同状态码的响应。响应的数据内容和数量可以调用user_mb.c文件中的接口更改。

```javascript
eMBException eMBFuncReadCoils( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncWriteCoil( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncWriteMultipleCoils( UCHAR * pucFrame, USHORT * usLen )
```
**描述:**
* 线圈的读写函数

```javascript
eMBException eMBFuncReadDiscreteInputs( UCHAR * pucFrame, USHORT * usLen )
```
**描述:**
* Discrete读函数

```javascript
eMBException eMBFuncWriteHoldingRegister( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncWriteMultipleHoldingRegister( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncReadHoldingRegister( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncReadWriteMultipleHoldingRegister( UCHAR * pucFrame, USHORT * usLen )
```
**描述:**
* HoldingRegister读写函数

```javascript
eMBException eMBFuncReadInputRegister( UCHAR * pucFrame, USHORT * usLen )
```
**描述:**
* InputRegiste读函数


* ##### 2.4 user_mb.c文件的接口
user_mb.c文件的接口主要用来更改tcp从机模式下，提供的响应内容、以及回调。

```javascript
eMBException eMBFuncReadCoils( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncWriteCoil( UCHAR * pucFrame, USHORT * usLen )
eMBException eMBFuncWriteMultipleCoils( UCHAR * pucFrame, USHORT * usLen )
```
**描述:**
* 线圈的读写函数

```javascript
eMBRegInput_Init(USHORT *bufAddress, USHORT usRegStart,UINT32_T usNRegs )
```
**描述:**
* InputRegiste模式下响应的内容。bufAddress为内容的地址，usRegStart为内容的索引，usNRegs为提供的内容个数

```javascript
eMBRegInputCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs )
```
**描述:**
* InputRegiste模式下的回调，该回调会被functions目录下的接口调用



```javascript
eMBRegHolding_Init(USHORT *bufAddress, USHORT usHolgStart,UINT32_T usHoldNRegs )
```
**描述:**
* HoldingRegiste模式下响应的内容。bufAddress为内容的地址，usHolgStart为内容的索引，usHoldNRegs为提供的内容个数

```javascript
eMBRegHoldingCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs, eMBRegisterMode eMode )
```
**描述:**
* HoldingRegiste模式下的回调，该回调会被functions目录下的接口调用


```javascript
eMBRegCoils_Init(UCHAR *bufAddress, USHORT usCoilStart,UINT32_T coilNRegs )
```
**描述:**
* Coils模式下响应的内容。bufAddress为内容的地址，usCoilStart为内容的索引，coilNRegs为提供的内容个数

```javascript
eMBRegCoilsCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNCoils, eMBRegisterMode eMode )
```
**描述:**
* Coils模式下的回调，该回调会被functions目录下的接口调用


```javascript
eMBRegDiscrete_Init(UCHAR *bufAddress, USHORT usdiscreteStart,UINT32_T discreteNRegs )
```
**描述:**
* Discrete模式下响应的内容。bufAddress为内容的地址，usdiscreteStart为内容的索引，discreteNRegs为提供的内容个数

```javascript
eMBRegDiscreteCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNDiscrete )
```
**描述:**
* Discrete模式下的回调，该回调会被functions目录下的接口调用

### 3. 测试用例
使用时配置menuconfig，打开modbus协议以后，默认使能tcp模式。下面的用例为tcp从机模式。
```javascript
    static void modbustcp_thread(void *arg)
    {	
        LWIP_UNUSED_ARG(arg);
        eMBErrorCode    xStatus = MB_ENOERR;
        
        /** 初始化不同模式下的响应内容*/
        input_buf = (USHORT *)os_malloc(REG_INPUT_NREGS * 2 + 1);
        if (input_buf == NULL) 
        {
            os_printf(LM_CMD, LL_ERR, "cannot alloc mem\n");
        }
        for(int i = 0;i<REG_INPUT_NREGS;i++)
        {
            input_buf[i] = i;
        }
        eMBRegInput_Init(input_buf, 0, REG_INPUT_NREGS);


        hold_buf = (USHORT *)os_malloc(REG_HOLD_NREGS * 2 + 1);
        if (hold_buf == NULL) 
        {
            os_printf(LM_CMD, LL_ERR, "cannot alloc mem\n");
        }
        for(int i = 0;i<REG_HOLD_NREGS;i++)
        {
            hold_buf[i] = i + 0x1000;
        }
        eMBRegHolding_Init(hold_buf, 0, REG_HOLD_NREGS);


        coil_buf = (UCHAR *)os_malloc(REG_COIL_NREGS/8 + 1);
        if (coil_buf == NULL) 
        {
            os_printf(LM_CMD, LL_ERR, "cannot alloc mem\n");
        }
        for(int i = 0;i<REG_COIL_NREGS/8;i++)
        {
            coil_buf[i] = i;
        }
        eMBRegCoils_Init(coil_buf, 0, REG_COIL_NREGS);


        discrete_buf = (UCHAR *)os_malloc(REG_DISCRETE_NREGS/8 + 1);
        if (discrete_buf == NULL) 
        {
            os_printf(LM_CMD, LL_ERR, "cannot alloc mem\n");
        }
        for(int i = 0;i<REG_DISCRETE_NREGS/8;i++)
        {
            discrete_buf[i] = i;
        }
        eMBRegDiscrete_Init(discrete_buf, 0, REG_DISCRETE_NREGS);
       
         /** 协议栈的初始化以及对主机的响应*/
        for( ;; )
        {
            if( eMBTCPInit( MB_TCP_PORT_USE_DEFAULT ) != MB_ENOERR )
            {
                printf("%s: can't initialize modbus stack!\r\n", PROG );
            }
            else if( eMBEnable(  ) != MB_ENOERR )
            {
                printf("%s: can't enable modbus stack!\r\n", PROG );
            }
            else
            {
                do
                {
                    xStatus = eMBPoll(  );
                }
                while( xStatus == MB_ENOERR );
            }
            printf("%s: xStatus error = %d!\r\n", PROG, xStatus);
            /* An error occured. Maybe we can restart. */
            ( void )eMBDisable(  );
            ( void )eMBClose(  );
        }


        if(input_buf != NULL)
        {
            os_free(input_buf);
        }
        if(hold_buf != NULL)
        {
            os_free(hold_buf);
        }
        if(coil_buf != NULL)
        {
            os_free(coil_buf);
        }
        if(discrete_buf != NULL)
        {
            os_free(discrete_buf);
        }
    }
```






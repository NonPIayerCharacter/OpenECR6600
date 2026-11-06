#include "mb.h"
#include "mbutils.h"

/* Private variables ---------------------------------------------------------*/
USHORT *usRegInputBuf = NULL;
USHORT usRegInputStart = 0;
UINT32_T usRegInputNRegs = 0;

//保持寄存器内容
USHORT *usRegHoldingBuf = NULL;
USHORT usRegHoldingStart = 0;
UINT32_T usRegHoldNRegs = 0;

//线圈状态
UCHAR *ucRegCoilsBuf = NULL;
USHORT usRegCoilStart = 0;
UINT32_T usCoilNRegs = 0;

//开关输入状态
UCHAR *ucRegDiscreteBuf = NULL;
USHORT usRegDiscreteStart = 0;
UINT32_T usDiscreteNRegs = 0;


/******************************************************************/
eMBErrorCode
eMBRegInput_Init(USHORT *bufAddress, USHORT usRegStart,UINT32_T usNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    usRegInputBuf = bufAddress;
    usRegInputStart = usRegStart;
    usRegInputNRegs = usNRegs;
    return eStatus;
}

eMBErrorCode
eMBRegInputCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    int             iRegIndex;

    /* it already plus one in modbus function method. */
    usAddress--;

    if( ( usAddress >= usRegInputStart )
        && ( usAddress + usNRegs <= usRegInputStart + usRegInputNRegs ) )
    {
        iRegIndex = ( int )( usAddress - usRegInputStart );
        while( usNRegs > 0 )
        {
            *pucRegBuffer++ = ( unsigned char )( usRegInputBuf[iRegIndex] >> 8 );
            *pucRegBuffer++ = ( unsigned char )( usRegInputBuf[iRegIndex] & 0xFF );
            iRegIndex++;
            usNRegs--;
        }
    }
    else
    {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

eMBErrorCode
eMBRegHolding_Init(USHORT *bufAddress, USHORT usHolgStart,UINT32_T usHoldNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    usRegHoldingBuf = bufAddress;
    usRegHoldingStart = usHolgStart;
    usRegHoldNRegs = usHoldNRegs;
    return eStatus;
}

eMBErrorCode
eMBRegHoldingCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs, eMBRegisterMode eMode )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    int             iRegIndex;

    /* it already plus one in modbus function method. */
    usAddress--;

    if( ( usAddress >= usRegHoldingStart ) &&
        ( usAddress + usNRegs <= usRegHoldingStart + usRegHoldNRegs ) )
    {
        iRegIndex = ( int )( usAddress - usRegHoldingStart );
        switch ( eMode )
        {
            /* Pass current register values to the protocol stack. */
        case MB_REG_READ:
            while( usNRegs > 0 )
            {
                *pucRegBuffer++ = ( UCHAR ) ( usRegHoldingBuf[iRegIndex] >> 8 );
                *pucRegBuffer++ = ( UCHAR ) ( usRegHoldingBuf[iRegIndex] & 0xFF );
                iRegIndex++;
                usNRegs--;
            }
            break;

            /* Update current register values with new values from the
             * protocol stack. */
        case MB_REG_WRITE:
            while( usNRegs > 0 )
            {
                usRegHoldingBuf[iRegIndex] = *pucRegBuffer++ << 8;
                usRegHoldingBuf[iRegIndex] |= *pucRegBuffer++;
                iRegIndex++;
                usNRegs--;
            }
        }
    }
    else
    {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

eMBErrorCode
eMBRegCoils_Init(UCHAR *bufAddress, USHORT usCoilStart,UINT32_T coilNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    ucRegCoilsBuf = bufAddress;
    usRegCoilStart = usCoilStart;
    usCoilNRegs = coilNRegs;
    return eStatus;
}
eMBErrorCode
eMBRegCoilsCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNCoils, eMBRegisterMode eMode )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    USHORT          iRegIndex , iRegBitIndex , iNReg;
    UCHAR *         pucCoilBuf;
    USHORT          COIL_START;
    USHORT          COIL_NCOILS;
    USHORT          usCoilStart;
    iNReg =  usNCoils / 8 + 1;

    pucCoilBuf = ucRegCoilsBuf;
    COIL_START = usRegCoilStart;
    COIL_NCOILS = usCoilNRegs;
    usCoilStart = usRegCoilStart;

    /* it already plus one in modbus function method. */
    usAddress--;

    if( ( usAddress >= COIL_START ) &&
        ( usAddress + usNCoils <= COIL_START + COIL_NCOILS ) )
    {
        iRegIndex = (USHORT) (usAddress - usCoilStart) / 8;
        iRegBitIndex = (USHORT) (usAddress - usCoilStart) % 8;
        switch ( eMode )
        {
        /* read current coil values from the protocol stack. */
        case MB_REG_READ:
            while (iNReg > 0)
            {
                *pucRegBuffer++ = xMBUtilGetBits(&pucCoilBuf[iRegIndex++],
                        iRegBitIndex, 8);
                iNReg--;
            }
            pucRegBuffer--;
            /* last coils */
            usNCoils = usNCoils % 8;
            /* filling zero to high bit */
            *pucRegBuffer = *pucRegBuffer << (8 - usNCoils);
            *pucRegBuffer = *pucRegBuffer >> (8 - usNCoils);
            break;

            /* write current coil values with new values from the protocol stack. */
        case MB_REG_WRITE:
            while (iNReg > 1)
            {
                xMBUtilSetBits(&pucCoilBuf[iRegIndex++], iRegBitIndex, 8,
                        *pucRegBuffer++);
                iNReg--;
            }
            /* last coils */
            usNCoils = usNCoils % 8;
            /* xMBUtilSetBits has bug when ucNBits is zero */
            if (usNCoils != 0)
            {
                xMBUtilSetBits(&pucCoilBuf[iRegIndex++], iRegBitIndex, usNCoils,
                        *pucRegBuffer++);
            }
            break;
        }
    }
    else
    {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}


eMBErrorCode
eMBRegDiscrete_Init(UCHAR *bufAddress, USHORT usdiscreteStart,UINT32_T discreteNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    ucRegDiscreteBuf = bufAddress;
    usRegDiscreteStart = usdiscreteStart;
    usDiscreteNRegs = discreteNRegs;
    return eStatus;
}
eMBErrorCode
eMBRegDiscreteCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNDiscrete )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    USHORT          iRegIndex , iRegBitIndex , iNReg;
    UCHAR *         pucDiscreteInputBuf;
    USHORT          DISCRETE_INPUT_START;
    USHORT          DISCRETE_INPUT_NDISCRETES;
    USHORT          usDiscreteInputStart;
    iNReg =  usNDiscrete / 8 + 1;

    pucDiscreteInputBuf = ucRegDiscreteBuf;
    DISCRETE_INPUT_START = usRegDiscreteStart;
    DISCRETE_INPUT_NDISCRETES = usDiscreteNRegs;
    usDiscreteInputStart = usRegDiscreteStart;

    /* it already plus one in modbus function method. */
    usAddress--;

    if ((usAddress >= DISCRETE_INPUT_START)
            && (usAddress + usNDiscrete    <= DISCRETE_INPUT_START + DISCRETE_INPUT_NDISCRETES))
    {
        iRegIndex = (USHORT) (usAddress - usDiscreteInputStart) / 8;
        iRegBitIndex = (USHORT) (usAddress - usDiscreteInputStart) % 8;

        while (iNReg > 0)
        {
            *pucRegBuffer++ = xMBUtilGetBits(&pucDiscreteInputBuf[iRegIndex++],
                    iRegBitIndex, 8);
            iNReg--;
        }
        pucRegBuffer--;
        /* last discrete */
        usNDiscrete = usNDiscrete % 8;
        /* filling zero to high bit */
        *pucRegBuffer = *pucRegBuffer << (8 - usNDiscrete);
        *pucRegBuffer = *pucRegBuffer >> (8 - usNDiscrete);
    }
    else
    {
        eStatus = MB_ENOREG;
    }

    return eStatus;
}

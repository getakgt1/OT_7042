




#ifndef _WLAN_P2P_H
#define _WLAN_P2P_H



#if CFG_ENABLE_WIFI_DIRECT


/* Service Discovery */
typedef struct _PARAM_P2P_SEND_SD_RESPONSE {
    PARAM_MAC_ADDRESS   rReceiverAddr;
    UINT_8              fgNeedTxDoneIndication;
    UINT_8              ucChannelNum;
    UINT_16	            u2PacketLength;
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_SEND_SD_RESPONSE, *P_PARAM_P2P_SEND_SD_RESPONSE;

typedef struct _PARAM_P2P_GET_SD_REQUEST {
    PARAM_MAC_ADDRESS   rTransmitterAddr;
    UINT_16	            u2PacketLength;
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_GET_SD_REQUEST, *P_PARAM_P2P_GET_SD_REQUEST;

typedef struct _PARAM_P2P_GET_SD_REQUEST_EX {
    PARAM_MAC_ADDRESS   rTransmitterAddr;
    UINT_16	            u2PacketLength;
    UINT_8                  ucChannelNum;            /* Channel Number Where SD Request is received. */
    UINT_8                  ucSeqNum;                   /* Get SD Request by sequence number. */
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_GET_SD_REQUEST_EX, *P_PARAM_P2P_GET_SD_REQUEST_EX;

typedef struct _PARAM_P2P_SEND_SD_REQUEST {
    PARAM_MAC_ADDRESS   rReceiverAddr;
    UINT_8              fgNeedTxDoneIndication;
    UINT_8              ucVersionNum;               /* Indicate the Service Discovery Supplicant Version. */
    UINT_16             u2PacketLength;
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_SEND_SD_REQUEST, *P_PARAM_P2P_SEND_SD_REQUEST;

/* Service Discovery 1.0. */
typedef struct _PARAM_P2P_GET_SD_RESPONSE {
    PARAM_MAC_ADDRESS   rTransmitterAddr;
    UINT_16             u2PacketLength;
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_GET_SD_RESPONSE, *P_PARAM_P2P_GET_SD_RESPONSE;

/* Service Discovery 2.0. */
typedef struct _PARAM_P2P_GET_SD_RESPONSE_EX {
    PARAM_MAC_ADDRESS   rTransmitterAddr;
    UINT_16             u2PacketLength;
    UINT_8              ucSeqNum;                       /* Get SD Response by sequence number. */
    UINT_8              aucPacketContent[0]; /*native 802.11*/
} PARAM_P2P_GET_SD_RESPONSE_EX, *P_PARAM_P2P_GET_SD_RESPONSE_EX;


typedef struct _PARAM_P2P_TERMINATE_SD_PHASE {
    PARAM_MAC_ADDRESS   rPeerAddr;
} PARAM_P2P_TERMINATE_SD_PHASE, *P_PARAM_P2P_TERMINATE_SD_PHASE;


/*! \brief Key mapping of BSSID */
typedef struct _P2P_PARAM_KEY_T
{
    UINT_32             u4Length;             /*!< Length of structure */
    UINT_32             u4KeyIndex;           /*!< KeyID */
    UINT_32             u4KeyLength;          /*!< Key length in bytes */
    PARAM_MAC_ADDRESS   arBSSID;              /*!< MAC address */
    PARAM_KEY_RSC       rKeyRSC;
    UINT_8              aucKeyMaterial[32];    /*!< Key content by above setting */
} P2P_PARAM_KEY_T, *P_P2P_PARAM_KEY_T;




/*--------------------------------------------------------------*/
/* Routines to handle command                                   */
/*--------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAddP2PKey(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetRemoveP2PKey(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetNetworkAddress(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetP2PMulticastList(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

/*--------------------------------------------------------------*/
/* Service Discovery Subroutines                                */
/*--------------------------------------------------------------*/
WLAN_STATUS
wlanoidSendP2PSDRequest(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSendP2PSDResponse(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidGetP2PSDRequest(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidGetP2PSDResponse(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    puQueryInfoLen
    );

WLAN_STATUS
wlanoidSetP2PTerminateSDPhase(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

#if CFG_SUPPORT_ANTI_PIRACY
WLAN_STATUS
wlanoidSetSecCheckRequest(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidGetSecCheckResponse(
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );
#endif

WLAN_STATUS
wlanoidSetNoaParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetOppPsParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetUApsdParam (
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryP2pPowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetP2pPowerSaveProfile (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetP2pSetNetworkAddress (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvSetBuffer,
    IN  UINT_32     u4SetBufferLen,
    OUT PUINT_32    pu4SetInfoLen
    );

WLAN_STATUS
wlanoidQueryP2pOpChannel(
    IN P_ADAPTER_T prAdapter,
    IN PVOID pvQueryBuffer,
    IN UINT_32 u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidQueryP2pVersion(
    IN P_ADAPTER_T prAdapter,
    IN PVOID pvQueryBuffer,
    IN UINT_32 u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );

WLAN_STATUS
wlanoidSetP2pSupplicantVersion(
    IN  P_ADAPTER_T       prAdapter,
    IN  PVOID             pvSetBuffer,
    IN  UINT_32           u4SetBufferLen,
    OUT PUINT_32          pu4SetInfoLen
    );

WLAN_STATUS
wlanoidSetP2pWPSmode (
    IN  P_ADAPTER_T prAdapter,
    IN  PVOID       pvQueryBuffer,
    IN  UINT_32     u4QueryBufferLen,
    OUT PUINT_32    pu4QueryInfoLen
    );

#if CFG_SUPPORT_P2P_RSSI_QUERY
WLAN_STATUS
wlanoidQueryP2pRssi(
    IN P_ADAPTER_T prAdapter,
    IN PVOID pvQueryBuffer,
    IN UINT_32 u4QueryBufferLen,
    OUT PUINT_32 pu4QueryInfoLen
    );
#endif


/*--------------------------------------------------------------*/
/* Callbacks for event indication                               */
/*--------------------------------------------------------------*/


#endif
#endif /* _WLAN_P2P_H */

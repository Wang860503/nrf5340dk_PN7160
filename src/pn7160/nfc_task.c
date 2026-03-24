#include <zephyr/kernel.h>

#include "Nfc.h"
#include "ndef_helper.h"
#include "nfc_thread.h" /* 用於訪問 nfc_run_flag */
#include "tool.h"

static unsigned short volatile nfc_mode = 0;

/* Simple NDEF message: URI "https://www.nxp.com" */
static const unsigned char NDEF_MESSAGE[] = {0x91, 0x01, 0x0E, 0x55, 0x04, 0x6E,
                                             0x78, 0x70, 0x2E, 0x63, 0x6F, 0x6D,
                                             0x2F, 0x00, 0x00, 0x00};

#define print_buf(x, y, z)                                     \
  {                                                            \
    int loop;                                                  \
    PRINTF(x);                                                 \
    for (loop = 0; loop < z; loop++) PRINTF("%.2x ", y[loop]); \
    PRINTF("\n");                                              \
  }

/* Discovery loop configuration according to the targeted modes of operation */
const unsigned char DiscoveryTechnologies[] = {
#if defined P2P_SUPPORT || defined RW_SUPPORT
    MODE_POLL | TECH_PASSIVE_NFCA,
    MODE_POLL | TECH_PASSIVE_NFCF,
#endif  // if defined P2P_SUPPORT || defined RW_SUPPORT
#ifdef RW_SUPPORT
    MODE_POLL | TECH_PASSIVE_NFCB,
    MODE_POLL | TECH_PASSIVE_15693,
#endif  // ifdef RW_SUPPORT
#ifdef P2P_SUPPORT
    /* Only one POLL ACTIVE mode can be enabled, if both are defined only NFCF
       applies */
    MODE_POLL | TECH_ACTIVE_NFC,
// MODE_POLL | TECH_ACTIVE_NFCA,
// MODE_POLL | TECH_ACTIVE_NFCF,
#endif  // ifdef P2P_SUPPORT
#if defined P2P_SUPPORT || defined CARDEMU_SUPPORT
    MODE_LISTEN | TECH_PASSIVE_NFCA,
#endif  // if defined P2P_SUPPORT || defined CARDEMU_SUPPORT
#if defined CARDEMU_SUPPORT
    MODE_LISTEN | TECH_PASSIVE_NFCB,
#endif  // if defined CARDEMU_SUPPORT
#ifdef P2P_SUPPORT
    MODE_LISTEN | TECH_PASSIVE_NFCF,
    MODE_LISTEN | TECH_ACTIVE_NFC,
// MODE_LISTEN | TECH_ACTIVE_NFCA,
// MODE_LISTEN | TECH_ACTIVE_NFCF,
#endif  // ifdef P2P_SUPPORT
};

/* Mode configuration according to the targeted modes of operation */
unsigned mode = 0
#ifdef CARDEMU_SUPPORT
                | NXPNCI_MODE_CARDEMU
#endif  // ifdef P2P_SUPPORT
#ifdef P2P_SUPPORT
                | NXPNCI_MODE_P2P
#endif  // ifdef CARDEMU_SUPPORT
#ifdef RW_SUPPORT
                | NXPNCI_MODE_RW
#endif  // ifdef RW_SUPPORT
    ;

#ifdef RW_SUPPORT
#ifdef RW_RAW_EXCHANGE
void PCD_MIFARE_scenario(void) {
#define BLK_NB_MFC 4
#define KEY_MFC 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
#define DATA_WRITE_MFC                                                    \
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, \
      0xcc, 0xdd, 0xee, 0xff

  bool status;
  unsigned char Resp[256];
  unsigned char RespSize;
  /* Authenticate sector 1 with generic keys */
  unsigned char Auth[] = {0x40, BLK_NB_MFC / 4, 0x10, KEY_MFC};
  /* Read block 4 */
  unsigned char Read[] = {0x10, 0x30, BLK_NB_MFC};
  /* Write block 4 */
  unsigned char WritePart1[] = {0x10, 0xA0, BLK_NB_MFC};
  unsigned char WritePart2[] = {0x10, DATA_WRITE_MFC};

  /* Authenticate */
  status = NxpNci_ReaderTagCmd(Auth, sizeof(Auth), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Authenticate sector %d failed with error 0x%02x\n", Auth[1],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Authenticate sector %d succeed\n", Auth[1]);

  /* Read block */
  status = NxpNci_ReaderTagCmd(Read, sizeof(Read), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Read block %d failed with error 0x%02x\n", Read[2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", Read[2]);
  print_buf(" ", (Resp + 1), RespSize - 2);

  /* Write block */
  status = NxpNci_ReaderTagCmd(WritePart1, sizeof(WritePart1), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Write block %d failed with error 0x%02x\n", WritePart1[2],
           Resp[RespSize - 1]);
    return;
  }
  status = NxpNci_ReaderTagCmd(WritePart2, sizeof(WritePart2), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Write block %d failed with error 0x%02x\n", WritePart1[2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Block %d written\n", WritePart1[2]);

  /* Read block */
  status = NxpNci_ReaderTagCmd(Read, sizeof(Read), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Read failed with error 0x%02x\n", Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", Read[2]);
  print_buf(" ", (Resp + 1), RespSize - 2);
}

void PCD_ISO15693_scenario(void) {
#define BLK_NB_ISO15693 8
#define DATA_WRITE_ISO15693 0x11, 0x22, 0x33, 0x44

  bool status;
  unsigned char Resp[256];
  unsigned char RespSize;
  unsigned char ReadBlock[] = {0x02, 0x20, BLK_NB_ISO15693};
  unsigned char WriteBlock[] = {0x02, 0x21, BLK_NB_ISO15693,
                                DATA_WRITE_ISO15693};

  status = NxpNci_ReaderTagCmd(ReadBlock, sizeof(ReadBlock), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0x00)) {
    PRINTF(" Read block %d failed with error 0x%02x\n", ReadBlock[2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", ReadBlock[2]);
  print_buf(" ", (Resp + 1), RespSize - 2);

  /* Write */
  status = NxpNci_ReaderTagCmd(WriteBlock, sizeof(WriteBlock), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Write block %d failed with error 0x%02x\n", WriteBlock[2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Block %d written\n", WriteBlock[2]);

  /* Read back */
  status = NxpNci_ReaderTagCmd(ReadBlock, sizeof(ReadBlock), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0x00)) {
    PRINTF(" Read block %d failed with error 0x%02x\n", ReadBlock[2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", ReadBlock[2]);
  print_buf(" ", (Resp + 1), RespSize - 2);
}

void PCD_ISO14443_3A_scenario(void) {
#define BLK_NB_ISO14443_3A 5
#define DATA_WRITE_ISO14443_3A 0x11, 0x22, 0x33, 0x44

  bool status;
  unsigned char Resp[256];
  unsigned char RespSize;
  /* Read block */
  unsigned char Read[] = {0x30, BLK_NB_ISO14443_3A};
  /* Write block */
  unsigned char Write[] = {0xA2, BLK_NB_ISO14443_3A, DATA_WRITE_ISO14443_3A};

  /* Read */
  status = NxpNci_ReaderTagCmd(Read, sizeof(Read), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Read block %d failed with error 0x%02x\n", Read[1],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", Read[1]);
  print_buf(" ", Resp, 4);
  /* Write */
  status = NxpNci_ReaderTagCmd(Write, sizeof(Write), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Write block %d failed with error 0x%02x\n", Write[1],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Block %d written\n", Write[1]);

  /* Read back */
  status = NxpNci_ReaderTagCmd(Read, sizeof(Read), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 1] != 0)) {
    PRINTF(" Read block %d failed with error 0x%02x\n", Read[1],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Read block %d:", Read[1]);
  print_buf(" ", Resp, 4);
}

void PCD_ISO14443_4_scenario(void) {
  bool status;
  unsigned char Resp[256];
  unsigned char RespSize;
  unsigned char SelectPPSE[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, 0x32, 0x50,
                                0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E,
                                0x44, 0x44, 0x46, 0x30, 0x31, 0x00};

  status = NxpNci_ReaderTagCmd(SelectPPSE, sizeof(SelectPPSE), Resp, &RespSize);
  if ((status == NFC_ERROR) || (Resp[RespSize - 2] != 0x90) ||
      (Resp[RespSize - 1] != 0x00)) {
    PRINTF(" Select PPSE failed with error %02x %02x\n", Resp[RespSize - 2],
           Resp[RespSize - 1]);
    return;
  }
  PRINTF(" Select NDEF Application succeed\n");
}
#endif  // ifdef RW_RAW_EXCHANGE

void displayCardInfo(NxpNci_RfIntf_t RfIntf) {
  switch (RfIntf.Protocol) {
    case PROT_T1T:
    case PROT_T2T:
    case PROT_T3T:
    case PROT_ISODEP:
      PRINTF(" - POLL MODE: Remote T%dT activated\n", RfIntf.Protocol);
      break;
    // case PROT_ISO15693:
    case PROT_T5T:
      PRINTF(" - POLL MODE: Remote ISO15693 card activated\n");
      break;
    case PROT_MIFARE:
      PRINTF(" - POLL MODE: Remote MIFARE card activated\n");
      break;
    default:
      PRINTF(" - POLL MODE: Undetermined target\n");
      return;
  }

  switch (RfIntf.ModeTech) {
    case (MODE_POLL | TECH_PASSIVE_NFCA):
      // tone_pn7150_detected();
      PRINTF("\tSENS_RES = 0x%.2x 0x%.2x\n", RfIntf.Info.NFC_APP.SensRes[0],
             RfIntf.Info.NFC_APP.SensRes[1]);
      print_buf("\tNFCID = ", RfIntf.Info.NFC_APP.NfcId,
                RfIntf.Info.NFC_APP.NfcIdLen);
      if (RfIntf.Info.NFC_APP.SelResLen != 0)
        PRINTF("\tSEL_RES = 0x%.2x\n", RfIntf.Info.NFC_APP.SelRes[0]);
      break;

    case (MODE_POLL | TECH_PASSIVE_NFCB):
      if (RfIntf.Info.NFC_BPP.SensResLen != 0)
        print_buf("\tSENS_RES = ", RfIntf.Info.NFC_BPP.SensRes,
                  RfIntf.Info.NFC_BPP.SensResLen);
      break;

    case (MODE_POLL | TECH_PASSIVE_NFCF):
      // tone_pn7150_detected();
      PRINTF("\tBitrate = %s\n",
             (RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424");
      if (RfIntf.Info.NFC_FPP.SensResLen != 0)
        print_buf("\tSENS_RES = ", RfIntf.Info.NFC_FPP.SensRes,
                  RfIntf.Info.NFC_FPP.SensResLen);
      break;

    case (MODE_POLL | TECH_PASSIVE_15693):
      // tone_pn7150_detected();
      print_buf("\tID = ", RfIntf.Info.NFC_VPP.ID,
                sizeof(RfIntf.Info.NFC_VPP.ID));
      PRINTF("\tAFI = 0x%.2x\n", RfIntf.Info.NFC_VPP.AFI);
      PRINTF("\tDSFID = 0x%.2x\n", RfIntf.Info.NFC_VPP.DSFID);
      break;

    default:
      break;
  }
}

void task_nfc_reader(NxpNci_RfIntf_t RfIntf) {
  /* For each discovered cards */
  while (nfc_run_flag) { /* 檢查運行標誌 */
    /* Display detected card information */
    displayCardInfo(RfIntf);
    // tone_pn7150_detected();
    /* What's the detected card type ? */
    switch (RfIntf.Protocol) {
      case PROT_T1T:
      case PROT_T2T:
      case PROT_T3T:
      case PROT_ISODEP:
#ifndef RW_RAW_EXCHANGE
        /* Process NDEF message read */
        NxpNci_ProcessReaderMode(RfIntf, READ_NDEF);
#ifdef RW_NDEF_WRITING
        RW_NDEF_SetMessage((unsigned char*)NDEF_MESSAGE, sizeof(NDEF_MESSAGE),
                           NdefPush_Cb);
        /* Process NDEF message write */
        NxpNci_ReaderReActivate(&RfIntf);
        NxpNci_ProcessReaderMode(RfIntf, WRITE_NDEF);
#endif  // ifdef RW_NDEF_WRITING
#else   // ifndef RW_RAW_EXCHANGE
        if (RfIntf.Protocol == PROT_ISODEP) {
          PCD_ISO14443_4_scenario();
        } else if (RfIntf.Protocol == PROT_T2T) {
          PCD_ISO14443_3A_scenario();
        }
#endif  // ifndef RW_RAW_EXCHANGE
        break;

      // case PROT_ISO15693:
      case PROT_T5T:
#ifdef RW_RAW_EXCHANGE
        /* Run dedicated scenario to demonstrate ISO15693 card management */
        PCD_ISO15693_scenario();
#endif  // ifdef RW_RAW_EXCHANGE
        break;

      case PROT_MIFARE:
#ifndef RW_RAW_EXCHANGE
        /* Process NDEF message read */
        NxpNci_ProcessReaderMode(RfIntf, READ_NDEF);
#ifdef RW_NDEF_WRITING
        RW_NDEF_SetMessage((unsigned char*)NDEF_MESSAGE, sizeof(NDEF_MESSAGE),
                           NdefPush_Cb);
        /* Process NDEF message write */
        NxpNci_ReaderReActivate(&RfIntf);
        NxpNci_ProcessReaderMode(RfIntf, WRITE_NDEF);
#endif  // ifdef RW_NDEF_WRITING
#else   // #ifndef RW_RAW_EXCHANGE
        /* Run dedicated scenario to demonstrate MIFARE card management */
        PCD_MIFARE_scenario();
#endif  // ifndef RW_RAW_EXCHANGE
        break;

      default:
        break;
    }

    /* If more cards (or multi-protocol card) were discovered (only same
     * technology are supported) select next one */
    if (RfIntf.MoreTags) {
      if (NxpNci_ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    }
    /* Otherwise leave */
    else
      break;

    /* 檢查運行標誌 */
    if (!nfc_run_flag) {
      break;
    }
    k_msleep(100);
  }

  /* Wait for card removal - 但只在 nfc_run_flag 為 true 時 */
  if (nfc_run_flag && nfc_mode != 2) {
    NxpNci_ProcessReaderMode(RfIntf, PRESENCE_CHECK);
  }
  PRINTF("[PN7160] CARD REMOVED\n");

  /* 如果 nfc_run_flag 為 false，不重新啟動發現 */
  if (!nfc_run_flag) {
    return; /* 直接返回，不重新啟動發現 */
  }

  /* Restart discovery loop */
  if (NxpNci_StopDiscovery() != NFC_SUCCESS) {
    PRINTF("Error: cannot stop discovery\n");
  }

  if (nfc_mode != 2) {
    NxpNci_StartDiscovery(DiscoveryTechnologies, sizeof(DiscoveryTechnologies));
  }
  // tone_pn7150_remove();
}
#endif  // ifdef RW_SUPPORT

#if defined P2P_SUPPORT || defined RW_SUPPORT
/* NDEF Push callback - called when NDEF message is successfully written */
void NdefPush_Cb(unsigned char* pNdefMessage, unsigned short NdefMessageSize) {
  (void)pNdefMessage; /* 未使用的參數 */
  (void)NdefMessageSize;
  PRINTF("[PN7160] NDEF message written successfully (size: %d)\n",
         NdefMessageSize);
}

void NdefPull_Cb(unsigned char* pNdefMessage, unsigned short NdefMessageSize) {
  unsigned char* pNdefRecord = pNdefMessage;
  NdefRecord_t NdefRecord;
  unsigned char save;

  if (pNdefMessage == NULL) {
    PRINTF("--- Provisioned buffer size too small or NDEF message empty \n");
    return;
  }

  while (pNdefRecord != NULL) {
    PRINTF("--- NDEF record received:\n");

    NdefRecord = DetectNdefRecordType(pNdefRecord);

    switch (NdefRecord.recordType) {
      case MEDIA_VCARD: {
        save = NdefRecord.recordPayload[NdefRecord.recordPayloadSize];
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = '\0';
        PRINTF("   vCard:\n%s\n", NdefRecord.recordPayload);
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = save;
      } break;

      case WELL_KNOWN_SIMPLE_TEXT: {
        save = NdefRecord.recordPayload[NdefRecord.recordPayloadSize];
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = '\0';
        PRINTF("   Text record: %s\n",
               &NdefRecord.recordPayload[NdefRecord.recordPayload[0] + 1]);
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = save;
      } break;

      case WELL_KNOWN_SIMPLE_URI: {
        save = NdefRecord.recordPayload[NdefRecord.recordPayloadSize];
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = '\0';
        PRINTF("   URI record: %s%s\n",
               ndef_helper_UriHead(NdefRecord.recordPayload[0]),
               &NdefRecord.recordPayload[1]);
        NdefRecord.recordPayload[NdefRecord.recordPayloadSize] = save;
      } break;

      case MEDIA_HANDOVER_WIFI: {
        unsigned char index = 0, i;

        PRINTF("--- Received WIFI credentials:\n");
        if ((NdefRecord.recordPayload[index] == 0x10) &&
            (NdefRecord.recordPayload[index + 1] == 0x0e))
          index += 4;
        while (index < NdefRecord.recordPayloadSize) {
          if (NdefRecord.recordPayload[index] == 0x10) {
            if (NdefRecord.recordPayload[index + 1] == 0x45) {
              PRINTF("- SSID = ");
              for (i = 0; i < NdefRecord.recordPayload[index + 3]; i++)
                PRINTF("%c", NdefRecord.recordPayload[index + 4 + i]);
              PRINTF("\n");
            } else if (NdefRecord.recordPayload[index + 1] == 0x03)
              PRINTF("- Authenticate Type = %s\n",
                     ndef_helper_WifiAuth(NdefRecord.recordPayload[index + 5]));
            else if (NdefRecord.recordPayload[index + 1] == 0x0f)
              PRINTF("- Encryption Type = %s\n",
                     ndef_helper_WifiEnc(NdefRecord.recordPayload[index + 5]));
            else if (NdefRecord.recordPayload[index + 1] == 0x27) {
              PRINTF("- Network key = ");
              for (i = 0; i < NdefRecord.recordPayload[index + 3]; i++)
                PRINTF("#");
              PRINTF("\n");
            }
            index += 4 + NdefRecord.recordPayload[index + 3];
          } else
            continue;
        }
      } break;

      case WELL_KNOWN_HANDOVER_SELECT:
        PRINTF("   Handover select version %d.%d\n",
               NdefRecord.recordPayload[0] >> 4,
               NdefRecord.recordPayload[0] & 0xF);
        break;

      case WELL_KNOWN_HANDOVER_REQUEST:
        PRINTF("   Handover request version %d.%d\n",
               NdefRecord.recordPayload[0] >> 4,
               NdefRecord.recordPayload[0] & 0xF);
        break;

      case MEDIA_HANDOVER_BT:
        print_buf("   BT Handover payload = ", NdefRecord.recordPayload,
                  NdefRecord.recordPayloadSize);
        break;

      case MEDIA_HANDOVER_BLE:
        print_buf("   BLE Handover payload = ", NdefRecord.recordPayload,
                  NdefRecord.recordPayloadSize);
        break;

      case MEDIA_HANDOVER_BLE_SECURE:
        print_buf("   BLE secure Handover payload = ", NdefRecord.recordPayload,
                  NdefRecord.recordPayloadSize);
        break;

      default:
        PRINTF("   Unsupported NDEF record, cannot parse\n");
        break;
    }
    pNdefRecord = GetNextRecord(pNdefRecord);
  }

  PRINTF("\n");
}
#endif  // if defined P2P_SUPPORT || defined RW_SUPPORT

#ifdef BOARD_NRF52840
void task_nfc_init(void) {
#else  /* #ifdef BOARD_NRF52840 */
void task_nfc(void) {
  NxpNci_RfIntf_t RfInterface;
#endif /* #ifdef BOARD_NRF52840 */
#ifdef CARDEMU_SUPPORT
  /* Register NDEF message to be sent to remote reader */
  T4T_NDEF_EMU_SetMessage((unsigned char*)NDEF_MESSAGE, sizeof(NDEF_MESSAGE),
                          NdefPush_Cb);
#endif  // ifdef CARDEMU_SUPPORT

#ifdef P2P_SUPPORT
  /* Register NDEF message to be sent to remote peer */
  P2P_NDEF_SetMessage((unsigned char*)NDEF_MESSAGE, sizeof(NDEF_MESSAGE),
                      NdefPush_Cb);
  /* Register callback for reception of NDEF message from remote peer */
  P2P_NDEF_RegisterPullCallback(NdefPull_Cb);
#endif  // ifdef P2P_SUPPORT

#ifdef RW_SUPPORT
  /* Register callback for reception of NDEF message from remote cards */
  RW_NDEF_RegisterPullCallback(NdefPull_Cb);
#endif  // ifdef RW_SUPPORT

  /* Open connection to NXPNCI device */
  if (NxpNci_Connect() == NFC_ERROR) {
    PRINTF("Error: cannot connect to NXPNCI device\n");
    return;
  }

  if (NxpNci_ConfigureSettings() == NFC_ERROR) {
    PRINTF("Error: cannot configure NXPNCI settings\n");
    return;
  }

  if (NxpNci_ConfigureMode(mode) == NFC_ERROR) {
    PRINTF("Error: cannot configure NXPNCI\n");
    return;
  }

  /* Start Discovery */
  if (NxpNci_StartDiscovery(DiscoveryTechnologies,
                            sizeof(DiscoveryTechnologies)) != NFC_SUCCESS) {
    PRINTF("Error: cannot start discovery\n");
    return;
  }

  PRINTF("[PN7160] task_nfc_init!!!\n");
#ifdef BOARD_NRF52840
}

void task_nfc(void) {
  NxpNci_RfIntf_t RfInterface;
  uint8_t errorFlag = 0;
  while (nfc_run_flag) { /* 檢查運行標誌 */
    if (errorFlag == 0) {
      // PRINTF("\nWAITING FOR DEVICE DISCOVERY\n");
    }
    /* Wait until a peer is discovered, but check nfc_run_flag */
    if (!nfc_run_flag) {
      break; /* 如果標誌為 false，退出循環 */
    }
    if (NxpNci_WaitForDiscoveryNotification(&RfInterface) == NFC_ERROR) {
      errorFlag = 1;
      goto detect_error;
    } else {
      errorFlag = 0;
    }
#else  /* #ifdef BOARD_NRF52840 */
  while (nfc_run_flag) { /* 檢查運行標誌 */
    PRINTF("\nWAITING FOR DEVICE DISCOVERY\n");

    /* Wait until a peer is discovered, but check nfc_run_flag periodically */
    while (nfc_run_flag &&
           NxpNci_WaitForDiscoveryNotification(&RfInterface) != NFC_SUCCESS) {
      k_msleep(10); /* 短暫延遲，避免過度佔用 CPU */
    }

    /* 如果 nfc_run_flag 為 false，退出循環 */
    if (!nfc_run_flag) {
      break;
    }
#endif /* #ifdef BOARD_NRF52840 */
#ifdef CARDEMU_SUPPORT
    /* Is activated from remote T4T ? */
    if ((RfInterface.Interface == INTF_ISODEP) &&
        ((RfInterface.ModeTech & MODE_MASK) == MODE_LISTEN)) {
      PRINTF(" - LISTEN MODE: Activated from remote Reader\n");
#ifndef CARDEMU_RAW_EXCHANGE
      NxpNci_ProcessCardMode(RfInterface);
#else
      PICC_ISO14443_4_scenario();
#endif
      PRINTF("READER DISCONNECTED\n");
    } else
#endif

#ifdef P2P_SUPPORT
      /* Is P2P discovery ? */
      if (RfInterface.Interface == INTF_NFCDEP) {
        if ((RfInterface.ModeTech & MODE_LISTEN) == MODE_LISTEN)
          PRINTF(" - P2P TARGET MODE: Activated from remote Initiator\n");
        else
          PRINTF(" - P2P INITIATOR MODE: Remote Target activated\n");

        /* Process with SNEP for NDEF exchange */
        NxpNci_ProcessP2pMode(RfInterface);

        PRINTF("PEER LOST\n");
      } else
#endif  // ifdef P2P_SUPPORT
#ifdef RW_SUPPORT
          if ((RfInterface.ModeTech & MODE_MASK) == MODE_POLL) {
        task_nfc_reader(RfInterface);
      } else
#endif  // ifdef RW_SUPPORT
      {
        PRINTF("WRONG DISCOVERY\n");
      }
#ifdef BOARD_NRF52840
  detect_error:
    if (!nfc_run_flag) {
      break; /* 如果標誌為 false，退出循環 */
    }
    break;  // Sleep(200);
#endif      /* #ifdef BOARD_NRF52840 */
    if (!nfc_run_flag) {
      break; /* 如果標誌為 false，退出循環 */
    }
    k_msleep(100);
  }

  /* 停止發現並斷開連接 */
  if (!nfc_run_flag) {
    PRINTF("[PN7160] Stopping NFC discovery...\n");
    NxpNci_StopDiscovery();
    NxpNci_Disconnect();
  }
}
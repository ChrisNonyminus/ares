constexpr u32 USB_DEBUG_ADDRESS_SIZE = 8 * 1024 * 1024;
constexpr u32 USB_DEBUG_ADDRESS = 0x0380'0000; // 0x04000000-DEBUG_ADDRESS_SIZE
constexpr u32 USB_BUFFERSIZE = 512;
// Data header related
#define USBHEADER_CREATE(type, left) (((type << 24) | (left & 0x00FFFFFF)))
// Use these to conveniently read the header from usb_poll()
#define USBHEADER_GETTYPE(header) ((header & 0xFF000000) >> 24)
#define USBHEADER_GETSIZE(header) ((header & 0x00FFFFFF))

/*********************************
       SummerCart64 macros
*********************************/

#define SC64_SDRAM_BASE (0x10000000)

#define SC64_REGS_BASE (0x1FFF0000)
#define SC64_REG_CFG_SR_CMD (SC64_REGS_BASE + 0x00)
#define SC64_REG_CFG_DATA_0 (SC64_REGS_BASE + 0x04)
#define SC64_REG_CFG_DATA_1 (SC64_REGS_BASE + 0x08)
#define SC64_REG_CFG_VERSION (SC64_REGS_BASE + 0x0C)

#define SC64_CFG_SR_CMD_ERROR (1 << 28)
#define SC64_CFG_SR_CPU_BUSY (1 << 30)

#define SC64_VERSION (0x53437632)

#define SC64_CMD_CFG_UPDATE ('C')
#define SC64_CMD_DEBUG_TX_READY ('S')
#define SC64_CMD_DEBUG_TX_DATA ('D')
#define SC64_CMD_DEBUG_RX_READY ('A')
#define SC64_CMD_DEBUG_RX_BUSY ('F')
#define SC64_CMD_DEBUG_RX_DATA ('E')
#define SC64_CMD_DEBUG_RESET ('B')
#define SC64_CMD_USB_READ_STATUS ('u')
#define SC64_CMD_USB_WRITE_STATUS ('U')
#define SC64_CMD_USB_READ ('m')
#define SC64_CMD_USB_WRITE ('M')

#define SC64_CFG_ID_SDRAM_WRITABLE (2)
#define SC64_CFG_ROM_WRITE_ENABLE 1

#define SC64_ARGS(args, a0, a1)                                                \
    {                                                                          \
        args[0] = (a0);                                                        \
        args[1] = (a1);                                                        \
    }

/*********************************
         Network macros
*********************************/

// Settings
#define NETWORK_MODE 1     // Enable/Disable debug mode
#define NETWORK_INIT_MSG 1 // Print a message when debug mode has initialized
#define USE_FAULTTHREAD 1  // Create a fault detection thread (libultra only)
#define OVERWRITE_OSPRINT                                                      \
    1 // Replaces osSyncPrintf calls with network_printf (libultra only)
#define MAX_COMMANDS 25 // The max amount of user defined commands possible

// Fault thread definitions (libultra only)
#define FAULT_THREAD_ID 13
#define FAULT_THREAD_PRI 125
#define FAULT_THREAD_STACK 0x2000

// USB thread definitions (libultra only)
#define USB_THREAD_ID 14
#define USB_THREAD_PRI 126
#define USB_THREAD_STACK 0x2000

// Network types defintions
#define NETTYPE_TEXT 0x01
#define NETTYPE_UDP_START_SERVER 0x02
#define NETTYPE_UDP_CONNECT 0x03
#define NETTYPE_UDP_DISCONNECT 0x04
#define NETTYPE_UDP_SEND 0x05
#define NETTYPE_URL_FETCH 0x06
#define NETTYPE_URL_DOWNLOAD 0x07
#define NETTYPE_URL_POST 0x08

typedef u32 USBCmdArgs[2];

struct SC64
{
    union
    {
        u32 sr;
        u32 cmd;
    };
    USBCmdArgs data;

    bool txready = true;
    std::atomic_bool rxready = false;
    bool rxbusy = false;

    struct
    {
        bool sdramWritable = false;
    } config;

    struct USBDataHeader
    {
        static constexpr u8 DMA_CC[4] = {'D', 'M', 'A', '@'};
        static constexpr u8 CMP_CC[4] = {'C', 'M', 'P', 'H'};
        u8 dmaSig[4];  // always {'D', 'M', 'A', '@'};
        u32 usbHeader; // see USBHEADER_CREATE
        // u8 cmpSig[4]; // always {'C', 'M', 'P', 'H'};
    };

    inline void PerformCmd()
    {
        u32 addr, len, hdr, i;
        u8 *tmp;
        static ENetHost *enetHost;
        static ENetAddress enetAddress;
        static ENetPeer *enetPeer;
        static ENetEvent enetEvent;
        u32 res;
        static u8 buffer[512];

        static std::thread waitThread;

        static u32 rxdata[2];

        static std::atomic_bool sending = false;

        // printf("Performing SC64 cmd %d\n", cmd);

        switch (cmd)
        {
        case SC64_CMD_USB_WRITE_STATUS:
        case SC64_CMD_DEBUG_TX_READY:
        {
            data[0] = ((u32)txready);
            sr = 0;
            break;
        }
        case SC64_CMD_USB_READ_STATUS:
        case SC64_CMD_DEBUG_RX_READY:
        {
            data[0] = htobe32(rxbusy ? (1 << 31) : 0); // header
            data[1] = 0; // ???
                         // TODO
            if (rxready)
            {
                data[0] = htobe32(rxdata[0] | (rxbusy ? (1 << 31) : 0));
                data[1] = (rxdata[1]);
                rxready = false;
            }
            rxdata[0] = 0;
            rxdata[1] = 0;
            sr = 0;
            break;
        }
        case SC64_CMD_DEBUG_TX_DATA:
        case SC64_CMD_USB_WRITE:
        {
            if (cmd == SC64_CMD_USB_WRITE) {

            addr = (data[0]) - SC64_SDRAM_BASE;
            } else addr = data[0];
            len = USBHEADER_GETSIZE(data[1]);
            if (addr == USB_DEBUG_ADDRESS)
            {
                txready = false;
                addr += SC64_SDRAM_BASE;
                // getting usb tx data
                if (cmd == SC64_CMD_DEBUG_TX_DATA) {
                    if (!((cartridge.rom.read<Word>(addr)) == 0x444D4140))
                    { // DMA@
                        printf(
                            "dma sig was incorrect (expected 'DMA@', got %08X)\n",
                            (cartridge.rom.read<Word>(addr)));
                        exit(1);
                    }
                    addr += 4;
                    hdr = (cartridge.rom.read<Word>(addr));
                    if (USBHEADER_GETSIZE(hdr) > len)
                    {
                        printf("data size was bigger than expected!\n");
                        exit(1);
                    }
                addr += 4;
                } else {
                    hdr = data[1];
                }
                //printf("size = %08X\n", (USBHEADER_GETSIZE(hdr)));
                tmp = new u8[(USBHEADER_GETSIZE(hdr))];
                for (i = 0; i < (USBHEADER_GETSIZE(hdr)); i++)
                {
                    tmp[i] = cartridge.rom.read<Byte>(addr + i);
                }
                if (!enetHost)
                {
                    if (enet_initialize() != 0)
                    {
                        printf("An error occurred while initializing ENet.\n");
                        exit(1);
                    }
                }

                //printf("hdr=%08X\n", hdr);

                switch (USBHEADER_GETTYPE(hdr))
                {
                case NETTYPE_UDP_START_SERVER: {
                    if (enetHost)
                        enet_host_destroy(enetHost);
                    int port = atoi((const char *)&tmp[0]);
                    enetAddress.host = ENET_HOST_ANY;
                    enetAddress.port = port;
                    enetHost = enet_host_create(&enetAddress, 3, 2, 0, 0);
                    if (!enetHost) {
                        printf("Error creating server.\n");
                        sr |= SC64_CFG_SR_CMD_ERROR;
                        delete[] tmp;
                        exit(1);
                    }
                    rxready = true;
                    // rxdata[0] = USBHEADER_CREATE(NETTYPE_UDP_START_SERVER,
                    //                              0);
                    // rxdata[1] = USBHEADER_CREATE(NETTYPE_UDP_START_SERVER,
                    //                              strlen((const char *)&tmp[0]));
                    strcpy((char *)&buffer[0], (const char *)&tmp[0]);
                    buffer[strlen((const char *)&tmp[0])] = '\0';
                    waitThread = std::thread(
                        [&]
                        {
                            while (true)
                            {
                                while (enet_host_service(enetHost, &enetEvent,
                                                         0))
                                {
                                    while (sending) {}
                                    switch (enetEvent.type)
                                    {

                                    case ENET_EVENT_TYPE_RECEIVE:
                                    {
                                        // printf("\nA packet of length %08X "
                                        //        "containing %s was received "
                                        //        "from %u on channel %u.\n",
                                        //        enetEvent.packet->dataLength,
                                        //        enetEvent.packet->data,
                                        //        enetEvent.peer->connectID,
                                        //        enetEvent.channelID);
                                        rxready = true;
                                        rxdata[0] = USBHEADER_CREATE(
                                            NETTYPE_UDP_SEND,
                                            0);
                                        rxdata[1] =
                                            enetEvent.packet->dataLength;
                                        memcpy(&buffer[0],
                                               enetEvent.packet->data,
                                               enetEvent.packet->dataLength);
                                        /* Clean up the packet now that we're
                                         * done using it. */
                                        enet_packet_destroy(enetEvent.packet);
                                        // while (rxready)
                                        // {
                                        // }
                                    }
                                    break;
                                    default:
                                        break;
                                    }
                                }
                            }
                        });
                    break;
                }
                case NETTYPE_UDP_CONNECT:
                {
                    if (enetHost)
                        enet_host_destroy(enetHost);
                    std::string hostname((const char *)&tmp[0]);
                    std::string host = hostname;
                    int port = 0;
                    size_t pos = 0;
                    std::string s = hostname;
                    if ((pos = s.find(':')) != std::string::npos)
                    {
                        host = s.substr(0, pos);
                        s.erase(0, pos + 1);
                        port = atoi(s.c_str());
                    }

                    enetHost = enet_host_create(
                        NULL /* create a client host */,
                        1 /* only allow 1 outgoing connection */,
                        2 /* allow up 2 channels to be used, 0 and 1 */,
                        0 /* assume any amount of incoming bandwidth */,
                        0 /* assume any amount of outgoing bandwidth */);

                    printf("Connecting to host %s:%d...\n", host.c_str(), port);
                    res = enet_address_set_host(&enetAddress, host.c_str());
                    enetAddress.port = port;
                    if (res != 0)
                    {
                        printf("Error setting host: %08X\n", res);
                        sr |= SC64_CFG_SR_CMD_ERROR;
                        delete[] tmp;
                        exit(1);
                    }
                    enetPeer =
                        enet_host_connect(enetHost, &enetAddress, 2, 0);
                    if (!enetPeer)
                    {
                        sr |= SC64_CFG_SR_CMD_ERROR;
                        delete[] tmp;
                        return;
                    }
                    printf("Connected to host %08X\n", enetPeer->address.host);
                    rxready = true;
                    // rxdata[0] = USBHEADER_CREATE(NETTYPE_UDP_CONNECT,
                    //                              0);
                    // rxdata[1] = USBHEADER_CREATE(NETTYPE_UDP_CONNECT,
                    //                              strlen(host.c_str()));
                    strcpy((char *)&buffer[0], host.c_str());
                    buffer[strlen(host.c_str())] = '\0';
                    waitThread = std::thread(
                        [&]
                        {
                            while (true)
                            {
                                while (enet_host_service(enetHost, &enetEvent,
                                                         0))
                                {
                                    while (sending) {}
                                    switch (enetEvent.type)
                                    {

                                    case ENET_EVENT_TYPE_RECEIVE:
                                    {
                                        // printf("\nA packet of length 0x%08X "
                                        //        "containing %s was received "
                                        //        "from %u on channel %u.\n",
                                        //        enetEvent.packet->dataLength,
                                        //        enetEvent.packet->data,
                                        //        enetEvent.peer->connectID,
                                        //        enetEvent.channelID);
                                        rxready = true;
                                        rxdata[0] = USBHEADER_CREATE(
                                            NETTYPE_UDP_SEND,
                                            0);
                                        rxdata[1] =
                                            enetEvent.packet->dataLength;
                                        memcpy(&buffer[0],
                                               enetEvent.packet->data,
                                               enetEvent.packet->dataLength);
                                        /* Clean up the packet now that we're
                                         * done using it. */
                                        enet_packet_destroy(enetEvent.packet);
                                        // while (rxready)
                                        // {
                                        // }
                                    }
                                    break;
                                    default:
                                        break;
                                    }
                                }
                            }
                        });
                    break;
                }
                case NETTYPE_UDP_SEND:
                {
                    ENetPacket *packet = enet_packet_create(
                        tmp, len, ENET_PACKET_FLAG_RELIABLE);
                    if (!enetHost) {
                        return;
                    }
                    sending = true;
                        enet_host_broadcast(enetHost, 0, packet);
                        //enet_host_flush(enetHost);
                        sending = false;
                    return;
                    break;
                }
                case NETTYPE_TEXT: {
                    //printf("network_printf: %s\n", (const char*)&tmp[0]);
                    break;
                }
                default:
                {
                    printf("Unknown datatype %d\n", USBHEADER_GETTYPE(hdr));
                    exit(1);
                }
                }

                sr = 0;
                delete[] tmp;
                txready = true;
            }
            break;
        }
        case SC64_CMD_DEBUG_RX_DATA:
        case SC64_CMD_USB_READ:
        {
            if (cmd == SC64_CMD_USB_READ) {

            addr = (data[0]);
            } else addr = data[0] + SC64_SDRAM_BASE;
            len = USBHEADER_GETSIZE(data[1]);
            //printf("reading %d bytes from %08X\n", len, addr);
            rxbusy = true;
            for (i = addr; i < addr + len; i++)
            {
                cartridge.rom.write<Byte>(i, buffer[i - addr]);
            }
            rxbusy = false;

            sr = 0;
            break;
        }
        case SC64_CMD_DEBUG_RX_BUSY:
        {
            // TODO
            data[0] = ((u32)rxbusy);
            sr = 0;
            break;
        }
        case SC64_CMD_CFG_UPDATE:
        {
            switch (be32toh(data[0]))
            {
            case SC64_CFG_ID_SDRAM_WRITABLE:
            {
                config.sdramWritable = (bool)data[1];
                break;
            }
            default:
                break;
            }
            sr = 0;
            break;
        }
        default:
            sr = 0;
            break;
        }
    }
};

extern SC64 sc64;

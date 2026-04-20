#include "ACATA.h"
#include "ACATAPI.h"
#include "common/Console.h"

#include "ACMACROS.h"

#include "ACATA_CMD_RESPONSES"

std::string ACATA::imgpath = "";
ACMEDIATYPE ACATA::MediaType;

int ACATA::device_probes[ACATA_DEVCNT] = {0,0};
int ACATA::last_device_probed = 0;
u32 ACATA::last_read = 0;
u32 ACATA::last_write = 0;
u32 atacmd_responding = -1;
u32 atacmd_response_traverse = 0;

u16 ACATA::read16(u32 addr) {
    last_read = addr;
    switch (addr)
    {
    case ACATA_BASE_PROBEADDR:
        return ACATA::cmd_handleR(addr);
    break;
    case ACATA_PROBEREG_2: ACATA::REGS[ACATA_R_STATUS] = 0; break;
    case ACATA_PROBEREG_0:
    case ACATA_PROBEREG_1:
    case ACATA_PROBEREG_3:
    case ACATA_DEVICE_SELECT:
    break;
    case ACATA_R_STATUS:
        if (ACATA::last_write == ACATA_R_STATUS && ACATA::cmd_handled == 0 && ACATA::last_device_probed == ACATA_UNIT0) {
            // reading R_STATUS after writing zero to it? this is the ACATA probe. we have to respond BUSY at least once for the driver to keep going
            // FIXME
            if (ACATA::REGS[ACATA_R_STATUS] & ATA_STAT_BUSY)
                CLRB(ACATA::REGS[ACATA_R_STATUS], ATA_STAT_BUSY);
            else
                ACATA::REGS[ACATA_R_STATUS] |= ATA_STAT_BUSY;
        }
    break;
    
    default:
        // TODO: move the error line here after development stage finished
        break;
    }
    return ACATA::REGS[addr];
}

#define MMIO_RESPOND(V, O, N) if (V == O) V = N


void ACATA::write16(u32 addr, u16 val) {
    last_write = addr;
    u16 V = val;
    switch (addr) {
    case ACATA_PROBEREG_0: V = 52; break;
    case ACATA_PROBEREG_1:
    case ACATA_PROBEREG_3:
    case ACATA_PROBEREG_2:
        // value 2: ata_probe()
    break;
    case ACATA_R_STATUS:
        ACATA::rstat_write_handle(val);
        return; // dont store this value, w:ata command | r:ata status
        break;
    case ACATA_DEVICE_SELECT:
        ACATA::last_device_probed = ((V & ACATA_UNIT1) != 0); // ACATA: 0x0: unit 0 | 0x10: unit 1
        break;
    
    case ACATA_BASE_PROBEADDR:
        ACATA::cmd_handleW(addr, val);
    break;

    default:
        // TODO: move the error line here after development stage finished
        break;
    }
    ACATA::REGS[addr] = V;
}

u32 ACATA::cmd_handled;
u32 ACATA::cmd_handledc;
atapi_packet_t ACATA::ata_c_packet;

void ACATA::cmd_handleW(u32 addr, u16 val) {
    switch (ACATA::cmd_handled) {
    case ATA_C_PACKET:
        if (ACATA::cmd_handledc < 6) { // packet is followed by a 6 word PIO
            ACATA::ata_c_packet.raw[ACATA::cmd_handledc++] = val;
            if (ACATA::cmd_handledc == 6) {
                ACATAPI::handle_cmd(ACATA::ata_c_packet);
                CLRB(ACATA::REGS[ACATA_R_STATUS], ATA_STAT_DRQ); // keep up DRQ only while the packet comes in?
            }
        }
        break;
    
    default:
        Console.Error("ACATA: writing from %X while no pending CMD", addr);
        break;
    }
}

u16 ACATA::cmd_handleR(u32 addr) {
    switch (ACATA::cmd_handled) {
    case -1: break;
    case ATA_C_IDENTIFY_PACKET_DEVICE:
        if (ACATA::cmd_handledc < 256) {
            return ATA_R_IDENTIFY_PACKET_DEVICE[ACATA::cmd_handledc++];
        } else {ACATA::cmd_handled = -1; CLRB(ACATA_STATUS, ATA_STAT_DRQ);}
        break;
    case ATA_C_SET_FEATURES:
    break;
    case ATA_C_PACKET:
    break;
    
    default:
        Console.Error("ACATA: reading from %X while no pending CMD", ACATA_BASE_PROBEADDR);
    }
    return ACATA::REGS[ACATA_BASE_PROBEADDR];
}

void ACATA::rstat_write_handle(u16 val) {
    switch (val) {
    case ATA_C_NOP:
        ACATA::cmd_handled = val;
        // the only situation when we will recieve a 0 (twice) to this reg, is during ACATA init.
        // here, the ata busy flag must be present at least once, on the following checks to this addr
        break;
    case ATA_C_IDENTIFY_PACKET_DEVICE:
        Console.Warning("ATA_C_IDENTIFY_PACKET_DEVICE");
        ACATA::cmd_handled = val;
        ACATA::cmd_handledc = 0;
        ACATA_STATUS |= ATA_STAT_DRQ;
    break;
    case ATA_C_PACKET:
        ACATA::cmd_handled = val;
        ACATA::cmd_handledc = 0;
        Console.Warning("ATA_C_PACKET: isDMA:%d", ACATA_ISDMA!=0);
        ACATA_STATUS |= ATA_STAT_DRQ;
        CLRB(ACATA::REGS[ACATA_R_STATUS], ATA_STAT_BUSY);
        CLRB(ACATA::REGS[ACATA_PROBEREG_2], ATA_STAT_ERR); // when packet is sent, ACCDVD fails if this bit is set or timeout consumed
    break;
    
    
    default:
        Console.Error("unhandled ATACMD %X", val);
        break;
    }
}

std::map<u32, u32> ACATA::REGS = {
    {ACATA_BASE_PROBEADDR, 0},
    {ACATA_PROBEREG_0,     0},
    {ACATA_PROBEREG_1,     0},
    {ACATA_PROBEREG_2,     0},
    {ACATA_PROBEREG_3,     0},
    {ACATA_DEVICE_SELECT,  0},
    {ACATA_R_STATUS,       0},
    // atapi_packet_send
    {0x16050000,           0},// 0;
    {0x16040000,           0},// 64;
    //{ACATA_DEVICE_SELECT,           0},// flag & 0x10;
    {0x16160000,           0},// (flag & 2) ^ 2;
    {0x16010000,           0},// flag & 1;
};

void ACATA_SETUP() {
    ACATA::TH::readBuffer = new u8[ACATA::TH::readBufferLen];
}

#include "common/Path.h"

void ACATA::SetImgPath(const char* S) {
	ACATA::imgpath = Path::ToNativePath(Path::GetDirectory(S));
	Console.WriteLn("arcade image lookup path set to '%s'\n", ACATA::imgpath.c_str());
}

void ACATA::SetEnv(std::string ata_img_path, std::string ata_img_filename, std::string Media) {
	//ACATA::imgpath = Path::ToNativePath(Path::GetDirectory(ata_img_path));
	Console.WarningFmt("ata_img_path '{}' | ata_img_filename '{}'", ata_img_path, ata_img_filename);
    ACATA::imgpath = Path::Combine(ata_img_path, ata_img_filename);
    ACATA::MediaType = ACMEDIATYPE_FROM_STRING(Media);
	Console.WarningFmt("ACENV: ata_img: '{}'", ACATA::imgpath);
	Console.WarningFmt("mediat: '{}'", (int)ACATA::MediaType);
}

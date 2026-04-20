#include "ACATAPI.h"
#include "ACATA.h"
#include "common/Console.h"

#include "ACMACROS.h"

void ACATAPI::handle_cmd(atapi_packet_t P) {
    switch (P.pkt.opcode) {
    case ATAPICMD::READ_10: {
        u32 transf_lba = U32FU16(P.pkt.lba_high, P.pkt.lba_lo);
        Console.Warning("ACATAPI:READ_10: tlen:%X, lba:%X", P.pkt.transf_len, transf_lba);
        ACATA::TH::nsector = P.pkt.transf_len;
        ACATA::TH::LBA = transf_lba;
        if (ACATA_ISDMA) ACATA::TH::PendTrasnfType = ACATA::TH::PTRNSF::ATAPI;
        }
        break;
    
    default:
        Console.Error("ACATAPI: CMD %02X : {con0:%02X, lba:%08X, resv:%02X, tlen:%04X, con1:%02X, con2:%02X, unk:%02X}",
             P.pkt.opcode, P.pkt.control0, U32FU16(P.pkt.lba_high, P.pkt.lba_lo), P.pkt.resv, P.pkt.transf_len,
             P.pkt.control1, P.pkt.control2, P.pkt.unknown
            );
        break;
    }
}

u16 ACATAPI::Read10(u32 lba, u16 tlen) {

}

void ACATAPI::Setup() {
    u32 SectorSizes[3] = {/*TODO: CHECK CD SECTOR SIZE*/0, ACATAPI::CONSTANTS::DVD_SECTORSIZE, 512};
    ACATA::TH::sectorsize = SectorSizes[ACATA::MediaType];
}
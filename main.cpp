#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
using namespace std;

const int SECTOR_SIZE                                      =             512;
const int MAX_RAID_DEVICES                                 =              16;
const int MAX_DEVICE_SECTORS                               = 1024 * 1024 * 2;
const int MIN_DEVICE_SECTORS                               =    1 * 1024 * 2;

const int RAID_STOPPED                                     = 0;
const int RAID_OK                                          = 1;
const int RAID_DEGRADED                                    = 2;
const int RAID_FAILED                                      = 3;

struct TBlkDev
{
    int              m_Devices;
    int              m_Sectors;
    //                          ( diskNr, secNr, data, secCnt )
    int           (* m_Read )  ( int, int, void *, int );
    int           (* m_Write ) ( int, int, const void *, int );
};
#endif /* __PROGTEST__ */

class CRaidVolume
{
public:
    CRaidVolume();
    static bool              Create                        ( const TBlkDev   & dev );
    int                      Start                         ( const TBlkDev   & dev );
    int                      Stop                          ( void );
    int                      Resync                        ( void );
    int                      Status                        ( void ) const;
    int                      Size                          ( void ) const;
    bool                     Read                          ( int               secNr,
                                                             void            * data,
                                                             int               secCnt );
    bool                     Write                         ( int               secNr,
                                                             const void      * data,
                                                             int               secCnt );
protected:
    int raidStatus;
    int raidServiceData;
    int raidFailedDrive;
    int sectorNum;
    int deviceNum;
                            //( diskNr, secNr, data, secCnt )
    int (*driveRead) ( int, int, void *, int );
    int (*driveWrite) ( int, int, const void *, int );

    bool WriteService(int driveID, int serviceData);
    int ReadService(int driveID);
    int getPhysicalSector(int secNum);
    int getPhysicalDrive(int secNum);
    int getParityDrive(int row);
    void XORSectors(char* result, const char *sector);
    bool calculateDegradedSector(char *result, int degDrive, int row);
};

int CRaidVolume::Start(const TBlkDev &dev) {

    sectorNum = dev.m_Sectors;
    deviceNum = dev.m_Devices;
    driveWrite = dev.m_Write;
    driveRead = dev.m_Read;

    raidFailedDrive = -1;
    raidStatus = RAID_OK;
    int zeroCnt = 0;

    int timestamps[MAX_RAID_DEVICES];

    // Go through drives and read timestamps
    for (int i = 0; i < deviceNum; i++){
        timestamps[i] = ReadService(i);

        // Checks if drive failed read
        if (timestamps[i] < 42){
            zeroCnt++;
            // if there were two failed drives
            if (zeroCnt >= 2){
                raidStatus = RAID_FAILED;
                return raidStatus;
            }
        }
    }

    int timestamp = 0;
    // If 0 == 1
    if (timestamps[0] == timestamps[1]){
        timestamp = timestamps[0];
        for (int i = 2; i < deviceNum; i++){
            if (timestamp != timestamps[i]){
                if(raidStatus == RAID_DEGRADED){
                    raidStatus = RAID_FAILED;
                    return raidStatus;
                }
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = i;
            }
        }
    } else {
        // 0 != 1 but 1 == 2
        if (timestamps[1] == timestamps[2]){
            raidStatus = RAID_DEGRADED;
            raidFailedDrive = 0;
            timestamp = timestamps[1];

        } else if (timestamps[0] == timestamps[2]) { //0 != 1 but 0 == 2
            raidStatus = RAID_DEGRADED;
            raidFailedDrive = 1;
            timestamp = timestamps[0];

        } else { // 0 != 1, 1 != 2, 0 != 2
            raidStatus = RAID_FAILED;
            return raidStatus;
        }

        // checks rest of the drives against the valid timestamp
        for (int i = 3; i < deviceNum; i++){
            if (timestamp != timestamps[i]){
                raidStatus = RAID_FAILED;
                return raidStatus;
            }
        }
    }
    //todo check this if this doesnt cause problems
    raidServiceData = timestamp;
    return raidStatus;
}

int CRaidVolume::Stop(void) {
    raidServiceData++;

    if (raidStatus == RAID_FAILED){
        raidStatus = RAID_STOPPED;
        return raidStatus;
    }

    for (int i = 0; i < deviceNum; i++){
        if (i == raidFailedDrive){ continue; }
        WriteService(i, raidServiceData);
    }

    raidStatus = RAID_STOPPED;
    return raidStatus;
}

bool CRaidVolume::Read(int secNr, void *data, int secCnt) {

    int physSector = 0;
    int physDrive = 0;

    char *dataTmp = (char*)data;

    //iterating through sectors if we read more of them
    for(int currentSector = secNr; currentSector < secCnt+secNr; currentSector++){
        physSector = getPhysicalSector(currentSector);
        physDrive = getPhysicalDrive(currentSector);

        // If all is okay reads sector
        if (raidStatus == RAID_OK){
            // If read fails turns drive to degraded
            int ret = driveRead(physDrive, physSector, dataTmp, 1);
            if ( ret != 1 ){
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = physDrive;
                currentSector--;
                continue;
            }
        }

        // If drive is degraded, tries to read sector, if its degraded sector calculates it
        if (raidStatus == RAID_DEGRADED){
            if (physDrive == raidFailedDrive){

                // If calculating failed drive fails then RAID FAILED
                if (!calculateDegradedSector(dataTmp, raidFailedDrive, physSector)){
                    raidStatus = RAID_FAILED;
                    break;
                }
            } else {
                int ret = driveRead(physDrive, physSector, dataTmp, 1);
                if ( ret != 1 ){
                    raidStatus = RAID_FAILED;
                    break;
                }
            }
        }

        // If raid fails then returns false
        if (raidStatus == RAID_FAILED){
            return false;
        }

        // Shifting pointer if more sectors read at the same time
        dataTmp = dataTmp + SECTOR_SIZE;
    }

    return true;
}

bool CRaidVolume::Write(int secNr, const void *data, int secCnt) {

    int physSector = 0;
    int physDrive = 0;
    int parityDrive = 0;
    int ret = 0;

    char *dataTmp = (char*)data;

    // Iterating through sectors if we write more of them
    for(int currentSector = secNr; currentSector < secCnt+secNr; currentSector++){
        physSector = getPhysicalSector(currentSector);
        physDrive = getPhysicalDrive(currentSector);
        parityDrive = getParityDrive(physSector);

        // prepping buffers for old stuff
        char oldData[SECTOR_SIZE];
        char oldParity[SECTOR_SIZE];

        // If raid is ok simply write
        if(raidStatus == RAID_OK){

            // Read old data from drive
            ret = driveRead(physDrive, physSector, oldData, 1);
            if (ret != 1){
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = physDrive;
                currentSector--;
                continue;
            }

            // Read old parity for given row
            ret = driveRead(parityDrive, physSector, oldParity, 1);
            if (ret != 1){
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = parityDrive;
                currentSector--;
                continue;
            }

            // Xor out old data
            XORSectors(oldParity, oldData);
            // XOR in new data
            XORSectors(oldParity, dataTmp);

            // Write new parity
            ret = driveWrite(parityDrive, physSector, oldParity, 1);
            if ( ret != 1 ){
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = parityDrive;
                currentSector--;
                continue;
            }

            // Write new data
            ret = driveWrite(physDrive, physSector, dataTmp, 1);
            if ( ret != 1 ){
                raidStatus = RAID_DEGRADED;
                raidFailedDrive = physDrive;
                currentSector--;
                continue;
            }

        }

        // If raid is degraded and we write into failed drive we recalculate parity
        // if failed drive is the one we want to write into, just recalculate parity
        if (raidStatus == RAID_DEGRADED){
            if (physDrive == raidFailedDrive){
                if (!calculateDegradedSector(oldData, physDrive, physSector)){
                    raidStatus = RAID_FAILED;
                    break;
                }

                // Read old parity for given row
                ret = driveRead(parityDrive, physSector, oldParity, 1);
                if (ret != 1){
                    raidStatus = RAID_FAILED;
                    break;
                }

                // Xor out old data
                XORSectors(oldParity, oldData);
                // XOR in new data
                XORSectors(oldParity, dataTmp);

                // Write new parity
                ret = driveWrite(parityDrive, physSector, oldParity, 1);
                if ( ret != 1 ){
                    raidStatus = RAID_FAILED;
                    break;
                }

            } else {
                ret = driveRead(physDrive, physSector, oldData, 1);
                if (ret != 1){
                    raidStatus = RAID_FAILED;
                    break;
                }

                // recalculate parity IF PARITY DRIVE WORKS
                if (raidFailedDrive != parityDrive){
                    // Read old parity for given row
                    ret = driveRead(parityDrive, physSector, oldParity, 1);
                    if (ret != 1){
                        raidStatus = RAID_FAILED;
                        break;
                    }

                    // Xor out old data
                    XORSectors(oldParity, oldData);
                    // XOR in new data
                    XORSectors(oldParity, dataTmp);

                    // Write new parity
                    ret = driveWrite(parityDrive, physSector, oldParity, 1);
                    if ( ret != 1 ){
                        raidStatus = RAID_FAILED;
                        break;
                    }
                }

                // Write new data
                ret = driveWrite(physDrive, physSector, dataTmp, 1);
                if ( ret != 1 ){
                    raidStatus = RAID_FAILED;
                    break;
                }
            }
        }

        if (raidStatus == RAID_FAILED){
            return false;
        }

        dataTmp = dataTmp + SECTOR_SIZE;
    }

    return true;
}

int CRaidVolume::Resync(void) {

    if (raidStatus == RAID_FAILED || raidStatus == RAID_STOPPED || raidStatus == RAID_OK){
        return raidStatus;
    }

    char sector[SECTOR_SIZE];

    for (int i = 0; i < sectorNum-1; i++){

        if (!calculateDegradedSector(sector, raidFailedDrive, i)){
            raidStatus = RAID_FAILED;
            return raidStatus;
        }

        int ret = driveWrite(raidFailedDrive, i, sector, 1);
        if (ret != 1){
            raidStatus = RAID_DEGRADED;
            return raidStatus;
        }

    }

    if (!WriteService(raidFailedDrive, raidServiceData)){
        raidStatus = RAID_DEGRADED;
        return raidStatus;
    }

    raidStatus = RAID_OK;
    raidFailedDrive = -1;
    return raidStatus;
}

void CRaidVolume::XORSectors(char* result, const char *sector) {

    // Iterating through array as bytes and xoring them
    for (int i = 0; i < SECTOR_SIZE; i++ ){
        result[i] = result[i] ^ sector[i];
    }

}

CRaidVolume::CRaidVolume(){
    raidStatus = RAID_STOPPED;
    raidServiceData = 0;
    sectorNum = 0;
    deviceNum = 0;
    driveRead = NULL;
    driveWrite = NULL;
}

bool CRaidVolume::Create(const TBlkDev &dev) {

    char sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);

    int *service = (int*)sector;
    service[0] = 42;

    // Writing initial service data to all drives' last sector
    for (int i = 0; i < dev.m_Devices; i++){
        int ret = dev.m_Write(i, dev.m_Sectors-1, service, 1);
        if (ret != 1){
            return false;
        }
    }
    return true;
}

//todo Check if the calculations work
int CRaidVolume::getPhysicalDrive(int secNum) {
    // for parity balancing
    // int row = getPhysicalSector(secNum)
    // get on which drive is parity
    // secNum % (deviceNum)
    // if >= parity then +1

    int row = getPhysicalSector(secNum);
    int parityDrive = getParityDrive(row);
    int drive = secNum % (deviceNum-1);

    if (drive >= parityDrive){
        drive++;
    }

    return drive;
}

int CRaidVolume::getParityDrive(int row){
    return row % deviceNum;
}

int CRaidVolume::getPhysicalSector(int secNum) {
    int drive = secNum % (deviceNum-1);
    int sector = (secNum - drive) / (deviceNum - 1);
    return sector;
}

bool CRaidVolume::WriteService(int driveID, int serviceData) {
    char sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);

    // Casting so we can simply write the value in it
    int *service = (int*)sector;

    service[0] = serviceData;

    // Writing service data to last sector
    int ret = driveWrite(driveID, sectorNum-1, sector, 1);
    if (ret != 1){
        return false;
    }

    return true;
}

int CRaidVolume::ReadService(int driveID) {
    char sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);

    // Casting so we can simply write the value in it
    int *service = (int*)sector;

    // Read service data from last sector
    int ret = driveRead(driveID, sectorNum-1, sector, 1);
    if (ret != 1){
        return -1;
    }

    return service[0];
}

int CRaidVolume::Status(void) const {
    return raidStatus;
}

int CRaidVolume::Size(void) const {
    // number of devides * sectornum gives max number of usable sectors
    // We need to remove sectors used for service
    int size = (deviceNum-1) * (sectorNum - 1);
    return size;
}

bool CRaidVolume::calculateDegradedSector(char *result, int degDrive, int row) {

    char sector[SECTOR_SIZE];
    memset(sector,0,SECTOR_SIZE);

    // todo check if i can use dataTmp
    // check if this data setting works
    for (int i = 0; i < SECTOR_SIZE; i++){
        result[i] = 0;
    }

    // Going through drives and picking particular sector as a row
    for (int i = 0; i < deviceNum; i++){
        // Skip bad drive
        if (i == degDrive){
            continue;
        }

        // read sector from drive
        int ret = driveRead(i, row, sector, 1);
        if (ret != 1){
            //raidFailedDrive = i;
            return false;
        }

        XORSectors(result, sector);
    }

    return true;
}


#ifndef __PROGTEST__
#include "tests.inc"
#endif /* __PROGTEST__ */
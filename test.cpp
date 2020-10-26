

/* SW RAID5 - basic test
 *
 * The testing of the RAID driver requires a backend (simulating the underlying disks).
 * Next, the tests of your RAID implemetnation are needed. To help you with the implementation,
 * a sample backend is implemented in this file. It provides a quick-and-dirty
 * implementation of the underlying disks (simulated in files) and a few Raid... function calls.
 *
 * The implementation in the real testing environment is different. The sample below is a
 * minimalistic disk backend which matches the required interface. The backend, for instance,
 * cannot simulate a crashed disk. To test your Raid implementation, you will have to modify
 * or extend the backend.
 *
 * Next, you will have to add some raid testing. There is a few Raid... functions called from within
 * main(), however, the tests are incomplete. For instance, RaidResync () is not tested here. Once
 * again, this is only a starting point.
 */


const int RAID_DEVICES = 4;
const int DISK_SECTORS = 4096;
int FAILED_DRIVE1 = -1;
int FAILED_DRIVE2 = -1;
static FILE  * g_Fp[RAID_DEVICES];

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your Raid driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
int                diskRead                                ( int               device,
                                                             int               sectorNr,
                                                             void            * data,
                                                             int               sectorCnt )
{
    if ( device < 0 || device >= RAID_DEVICES || device == FAILED_DRIVE1  || device == FAILED_DRIVE2)
        return 0;
    if ( g_Fp[device] == NULL )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int                diskWrite                               ( int               device,
                                                             int               sectorNr,
                                                             const void      * data,
                                                             int               sectorCnt )
{
    if ( device < 0 || device >= RAID_DEVICES || device == FAILED_DRIVE1  || device == FAILED_DRIVE2)
        return 0;
    if ( g_Fp[device] == NULL )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fwrite ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** A function which releases resources allocated by openDisks/createDisks
 */
void               doneDisks                               ( void )
{
    for ( int i = 0; i < RAID_DEVICES; i ++ )
        if ( g_Fp[i] )
        {
            fclose ( g_Fp[i] );
            g_Fp[i]  = NULL;
        }
}
//-------------------------------------------------------------------------------------------------
/** A function which creates the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev            createDisks                             ( void )
{
    char       buffer[SECTOR_SIZE];
    TBlkDev    res;
    char       fn[100];

    memset    ( buffer, 0, sizeof ( buffer ) );

    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "%04d", i );
        g_Fp[i] = fopen ( fn, "w+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw "Raw storage create error";
        }

        for ( int j = 0; j < DISK_SECTORS; j ++ )
            if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[i] ) != 1 )
            {
                doneDisks ();
                throw "Raw storage create error";
            }
    }

    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}
//-------------------------------------------------------------------------------------------------
/** A function which opens the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev            openDisks                               ( void )
{
    TBlkDev    res;
    char       fn[100];

    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "%04d", i );
        g_Fp[i] = fopen ( fn, "r+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw "Raw storage access error";
        }
        fseek ( g_Fp[i], 0, SEEK_END );
        if ( ftell ( g_Fp[i] ) != DISK_SECTORS * SECTOR_SIZE )
        {
            doneDisks ();
            throw "Raw storage read error";
        }
    }
    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}

void testMultipleSectors(CRaidVolume &vol){

    for ( int i = 0; i < vol . Size (); i ++ )
    {
        char buffer [SECTOR_SIZE];

        assert ( vol . Read ( i, buffer, 1 ) );
        assert ( vol . Write ( i, buffer, 1 ) );
    }

    char * refData = new char[vol.Size() * SECTOR_SIZE];
    char * readData = new char[vol.Size() * SECTOR_SIZE];

    for( int i = 0; i < vol.Size() * SECTOR_SIZE; ++i )
    {
        refData[i] = i % 250 + 7;
        readData[i] = 0;
    }

    bool write = vol.Write( 0, refData, vol.Size() );
    FAILED_DRIVE1 = 0;
    int state = vol.Resync();
    bool read = vol.Read( 0, readData, vol.Size() );
    assert( !memcmp( refData, readData, vol.Size() ) );

    delete[] refData;
    delete[] readData;
}

void testDegradedRandom(CRaidVolume & vol, TBlkDev &dev){

    FAILED_DRIVE1 = RAID_DEVICES-1;
    FAILED_DRIVE2 = -1;

    // Number of sectors available
    char **savedSectors = new char *[vol.Size()];
    for (int i = 0; i < vol.Size(); i++){
        // Allocate size for each sector
        savedSectors[i] = new char [SECTOR_SIZE];
        memset(savedSectors[i], 0, SECTOR_SIZE);
    }

    char randSector[SECTOR_SIZE];
    int uwu = 1;
    while (uwu){

        // Random sector number
        int randSectorNum = rand() % vol.Size();

        int i = rand() % 3;
        //i = 3;
        if (i == 0){
            // Generates a random sector
            memset(randSector, 0, SECTOR_SIZE);
            for (int i; i < SECTOR_SIZE; i++){
                int rnd = rand() % 256;
                randSector[i] = rnd;
            }

            // Writes random sector
            if (vol.Write(randSectorNum, randSector, 1)){
                memcpy(savedSectors[randSectorNum], randSector, SECTOR_SIZE);
            } else {
                printf("WRITE FAIL\n");
                continue;
            }

            printf("WRITE SUCCESS\n");

        } else if (i == 1){

            if (vol.Read(randSectorNum, randSector, 1)){
                if (memcmp( randSector, savedSectors[randSectorNum], SECTOR_SIZE) != 0){
                    printf("READ DOESNT MATCH\n");
                }
            } else {
                printf("READ FAIL\n");
                continue;
            }

            printf("READ SUCCESS\n");
        } else {

            int status = vol.Stop();
            if (status != RAID_STOPPED || status != vol.Status()){
                printf("STOP STATUS FAIL\n");
                continue;
            }

            status = vol.Start(dev);
            if (status != RAID_DEGRADED || status != vol.Status()){
                printf("START STATUS FAIL\n");
                continue;
            }
            printf("STOP/START SUCCESS\n");
        }
    }

}

//-------------------------------------------------------------------------------------------------
void               test1                                   ( void )
{
    /* create the disks before we use them
     */
    TBlkDev  dev = createDisks ();
    /* The disks are ready at this moment. Your RAID-related functions may be executed,
     * the disk backend is ready.
     *
     * First, try to create the RAID:
     */
    assert ( CRaidVolume::Create ( dev ) );
    /* start RAID volume */
    CRaidVolume vol;

    //FAILED_DRIVE1 = 0;

    assert ( vol . Start ( dev ) == RAID_OK );
    assert ( vol . Status () == RAID_OK );

    /* your raid device shall be up.
     * try to read and write all RAID sectors:
     */

    //testMultipleSectors(vol);
    testDegradedRandom(vol, dev);
    assert ( vol . Stop () == RAID_STOPPED );
    assert ( vol . Status () == RAID_STOPPED );

    /* ... and the underlying disks.
     */

    doneDisks ();
}
//-------------------------------------------------------------------------------------------------
void               test2                                   ( void )
{
    /* The RAID as well as disks are stopped. It corresponds i.e. to the
     * restart of a real computer.
     *
     * after the restart, we will not create the disks, nor create RAID (we do not
     * want to destroy the content). Instead, we will only open/start the devices:
     */

    TBlkDev dev = openDisks ();
    CRaidVolume vol;

    assert ( vol . Start ( dev ) == RAID_OK );


    /* some I/O: RaidRead/RaidWrite
     */

    vol . Stop ();
    doneDisks ();
}
//-------------------------------------------------------------------------------------------------
int                main                                    ( void )
{
    test1 ();
    test2 ();
    return 0;
}






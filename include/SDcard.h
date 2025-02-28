// SDcard.h
// Includes for SD card interface.
// The tracklist will be written to a file on the SD card itself.
//

#define MAXFNLEN 254   // max length of a full filespec
#define SD_MAXDEPTH 4  // maximum depths
#define MAXFOLDERS 63  // maximum number of folders on an SD card
#define MAXFOLDLEN 150 // maximum folder name length (including all parent folderss)
#define MAXFILES 100   // maximum number of files in one folder
#define MAXFILELEN 100 // maximum file name length (without parent folder names)

struct mp3spec_t // For List of mp3 file on SD card
{
  uint8_t entrylen = 0;             // Total length of this entry
  uint8_t prevlen = 0;              // Number of chars that are equal to previous
  char filespec[MAXFNLEN + 1] = ""; // Full file spec, the divergent end part
};

struct shortmp3spec_t // For List of mp3 file on SD card
{
  uint8_t entrylen = 0; // Total length of this entry
  uint8_t prevlen = 0;  // Number of chars that are equal to previous
};

#include <SD_MMC.h>
#include <FS.h>
#define TRACKLIST "/tracklist.dat" // File with tracklist on SD card

mp3spec_t mp3entry; // Entry with track spec
char *SD_lastmp3spec; // Full file spec
char *mp3spec;        // Work full file spec
char *fullName;       // for trackfile and tracklist creating
File trackfile;                // File for tracknames
bool trackfile_isopen = false; // True if trackfile is open for read
static int folderNum = 0;
static int fileNum = 0;
char *folders[MAXFOLDERS]; // array of pointers to char buffers (MAXFOLDLEN bytes long each)
char *files[MAXFILES];     // array of pointers to char buffers (MAXFILELEN bytes long each)
uint8_t *trackBuf;

// Forward declaration
void SDtask(void *parameter);

const char *STAG = "SDcard";

//**************************************************************************************************
//                               C L O S E T R A C K F I L E                                       *
//**************************************************************************************************
void closeTrackfile()
{
  if (trackfile_isopen) // Trackfile still open?
  {
    trackfile.close();
  }
  trackfile_isopen = false; // Reset open status
}

//**************************************************************************************************
//                                  O P E N T R A C K F I L E                                       *
//**************************************************************************************************
bool openTrackfile(const char *mode = FILE_READ)
{
  closeTrackfile();                         // Close if already open
  trackfile = SD_MMC.open(TRACKLIST, mode); // Open the tracklist file on SD card
  ESP_LOGW(STAG, "OpenTrackfile(%s) result is %d",
           mode, (int)trackfile);
  trackfile_isopen = (trackfile != 0); // Check result
  return trackfile_isopen;             // Return open status
}

//**************************************************************************************************
//                                   S E T S D F I L E N A M E                                     *
//**************************************************************************************************
// Set the current filespec.                                                                       *
//**************************************************************************************************
void setSDFileName(const char *fnam)
{
  if (strlen(fnam) <= MAXFNLEN) // Guard against long filenames
  {
    strcpy(SD_lastmp3spec, fnam); // Copy filename into SD_last
  }
}

//**************************************************************************************************
//                                   G E T S D F I L E N A M E                                     *
//**************************************************************************************************
// Get a filespec from tracklist file at index.                                                    *
//**************************************************************************************************
char *getSDFileName(int inx)
{
  int sdix = -1;   //-1 means that the loop will run exactly once for inx==0, twice for inx==1, etc.
  mp3spec_t entry; // Entry with track spec

  if (!trackfile_isopen) // Track file available?
  {
    return NULL; // No, return empty name
  }
  if (inx < 0) // Negative track number?
  {
    inx = 0;
  }
  if (inx >= SD_filecount) // Protect against going beyond last track
  {
    inx = 0; // Beyond last track: rewind to begin
  }
  trackfile.seek(0);
  while (sdix < inx) // Move forward until at required position
  {
    uint8_t *pm = (uint8_t *)&entry;            // Point to entrylen of mp3entry
    trackfile.read(pm, sizeof(entry.entrylen)); // Get total size of entry
    pm += sizeof(entry.entrylen);               // Bump pointer
    trackfile.read(pm, entry.entrylen -         // Read rest of entry
                           sizeof(entry.entrylen));
    strcpy(mp3spec + entry.prevlen, // Copy filename into SD_last
           entry.filespec);
    sdix++; // Set current index
  }
  return mp3spec; // Return pointer to filename
}

//**************************************************************************************************
//                              G E T S H O R T S D F I L E N A M E                                *
//**************************************************************************************************
// Get a short filename from mp3spec variable. Called from main.cpp.                               *
//**************************************************************************************************
char *getShortSDFileName()
{
  strncpy(shorttrname, SPACES, 32); // reset buffer to spaces
  strncpy(shorttrname, strrchr(mp3spec, '/') + 1, 128);
  return shorttrname;
}

//**************************************************************************************************
//                               G E T N E X T S D F I L E N A M E                                 *
//**************************************************************************************************
// Get next filespec from mp3names. Called from main.cpp.                                          *
//**************************************************************************************************
bool getNextSDFileName()
{
  int tmpix = SD_curindex + 1;
  if (tmpix >= SD_filecount)
  {
    if (!loop_)
    {
      return false;
    }
    tmpix = 0;
  }
  SD_curindex = tmpix;
  getSDFileName(SD_curindex);
  return true;
}

//**************************************************************************************************
//                                    G E T N E X T E N T R Y                                      *
// Gets the full file specification of the next tracklist item.                                    *
//**************************************************************************************************
char *getNextEntry()
{
  shortmp3spec_t entr;                       // Entry with track spec
  uint8_t *pm = (uint8_t *)&entr;            // Point to entrylen of mp3entry
  uint8_t *fn = (uint8_t *)fullName;         // Point to entrylen of fullName buffer
  trackfile.read(pm, sizeof(entr.entrylen)); // Get total size of entry
  pm += sizeof(entr.entrylen);               // Bump pointer
  trackfile.read(pm, sizeof(entr.entrylen)); // Read rest of entry
  pm += sizeof(entr.prevlen);                // Bump pointer
  trackfile.read(fn + entr.prevlen,          // Read rest of entry
                 entr.entrylen - sizeof(entr.entrylen) - sizeof(entr.prevlen));
  return fullName;
}

//**************************************************************************************************
//                      G E T C U R R E N T S H O R T S D F I L E N A M E                          *
//**************************************************************************************************
// Get last part of current filespec from mp3names.                                                *
//**************************************************************************************************
char *getCurrentShortSDFileName()
{
  strncpy(shorttrname, SPACES, 32); // reset buffer to spaces
  strncpy(shorttrname, strrchr(SD_lastmp3spec, '/') + 1, 128);
  return shorttrname;
}

//**************************************************************************************************
//                                  A D D T O F I L E L I S T                                      *
//**************************************************************************************************
// Add a filename to the trackfile on SD card.                                                     *
// Example:                                                                                        *
// Entry entry prev   filespec                             Interpreted result                      *
//  num   len   len
// ----- ----- ----   --------------------------------     ------------------------------------    *
// [0]      32    0   /Fleetwood Mac/Albatross.mp3         /Fleetwood Mac/Albatross.mp3            *
// [1]      15   15   Hold Me.mp3                          /Fleetwood Mac/Hold Me.mp3              *
//**************************************************************************************************
bool addToFileList(const char *newfnam)
{
  char *lnam = SD_lastmp3spec;  // Set pointer to compare
  uint8_t n = 0;                // Counter for number of equal characters
  uint16_t l = strlen(newfnam); // Length of new file name
  uint8_t entrylen;             // Length of entry
  bool res = false;             // Function result

  ESP_LOGW(STAG, "Full filename is %s", newfnam);
  if (l >= (MAXFNLEN)) // Block very long filenames
  {
    ESP_LOGE(STAG, "SD filename too long (%d)!", l);
    return false; // Filename too long: skip
  }
  while (*lnam == *newfnam) // Compare next character of filename
  {
    if (*lnam == '\0') // End of name?
    {
      break; // Yes, stop
    }
    n++;    // Equal: count
    lnam++; // Update pointers
    newfnam++;
  }
  mp3entry.prevlen = n;             // This part is equal to previous name
  l -= n;                           // Length of rest of filename
  if (l >= (sizeof(mp3spec_t) - 5)) // Block very long filenames
  {
    ESP_LOGE(STAG, "SD filename too long (%d)!", l);
    return false; // Filename too long: skip
  }
  strcpy(mp3entry.filespec, newfnam);    // This is last part of new filename
  entrylen = sizeof(mp3entry.entrylen) + // Size of entry including string delimeter
             sizeof(mp3entry.prevlen) +
             strlen(newfnam) + 1;
  mp3entry.entrylen = entrylen;
  strcpy(lnam, newfnam); // Set a new lastmp3spec
#if defined(DISP)
  ESP_LOGW(STAG, "Added %3u : %s", n, // Show last part of filename
           getCurrentShortSDFileName());
#endif
  if (trackfile_isopen) // Outputfile open?
  {
    res = true;                           // Yes, positive result
    trackfile.write((uint8_t *)&mp3entry, // Yes, add to list
                    entrylen);
    SD_filecount++; // Count number of files in list
  }
  return res; // Return result of adding name
}

//**************************************************************************************************
//                                C O M P A R E S T R I N G S                                      *
//**************************************************************************************************
// Helper function for qsort().                                                                    *
// Returns a positive result when the first argument is greater than the second one.               *
//**************************************************************************************************
int CompareStrings(const void *p1, const void *p2)
{
  char *s1 = *(char **)p1;
  char *s2 = *(char **)p2;
  return strcmp(s1, s2);
}

//**************************************************************************************************
//                                        L I S T F I L E S                                        *
//**************************************************************************************************
// Search all MP3 files on directory "dirname" of SD card.                                                   *
//**************************************************************************************************
bool listFiles(const char *dirname)
{
  File root; // Work directory
  File file; // File in work directory

  fileNum = 0; // Reset counter
  root = SD_MMC.open(dirname); // Open directory
  if (!root)                   // Check on open
  {
    ESP_LOGW(STAG, "Failed to open directory");
    return false;
  }
  if (!root.isDirectory()) // Really a directory?
  {
    ESP_LOGW(STAG, "Not a directory");
    return false;
  }
  file = root.openNextFile();
  while (file)
  {
    vTaskDelay(50 / portTICK_PERIOD_MS); // Allow others
    if (!file.isDirectory())             // Is it a file?
    {
      size_t namelen = strlen(file.name());
      if (namelen < MAXFILELEN)
      {
        strcpy(files[fileNum++], file.name());
        if (fileNum > MAXFILES)
        {
          ESP_LOGW(STAG, "Too many files !!!");
          return true;
        }
      }
      else
      {
        ESP_LOGW(STAG, "File name %s too long (%d)!", file.name(), namelen);
      }
    }
    file = root.openNextFile();
  }
  return true;
}

//**************************************************************************************************
//                                         L I S T D I R S                                         *
//**************************************************************************************************
// Search all directories on SD card.                                                              *
// Will be called recursively.                                                                     *
// Returns the number of folders found.                                                            *
//**************************************************************************************************
uint8_t listDirs(const char *dirname, uint8_t levels)
{
  File root; // Work directory
  File file; // File in work directory
  uint8_t oldFolderNum = folderNum;

  root = SD_MMC.open(dirname); // Open directory
  if (!root)                   // Check on open
  {
    ESP_LOGW(STAG, "Failed to open directory");
    return 0;

  }
  if (!root.isDirectory()) // Really a directory?
  {
    ESP_LOGW(STAG, "Not a directory");
    return 0;
  }

  size_t namelen = strlen(dirname);
  if (namelen < MAXFOLDLEN)
  {
    strcpy(folders[folderNum++], dirname);//crash
    if (folderNum >= MAXFOLDERS)
    {
      ESP_LOGW(STAG, "Too many folders !!!");
      return oldFolderNum - folderNum;
    }
  }
  else
  {
    ESP_LOGW(STAG, "Folder name %s too long (%d)!", file.name(), namelen);
  }
  file = root.openNextFile();
  while (file)
  {
    vTaskDelay(50 / portTICK_PERIOD_MS); // Allow others
    if (file.isDirectory())              // Is it a directory?
    {
      if (levels) // Dig in subdirectory?
      {
        if (strrchr(file.path(), '/')[1] != '.') // Skip hidden directories
        {
          if (folderNum < MAXFOLDERS)
          {
            if (!listDirs(file.path(), // Non hidden directory: call recursive
                        levels - 1))
            {
              return oldFolderNum - folderNum;
            }
          }
        }
      }
    }
    file = root.openNextFile();
  }
  return oldFolderNum - folderNum;
}

//**************************************************************************************************
//                                     T O L O W E R C A S E                                       *
//**************************************************************************************************
// Helper function for getsdtracks().                                                              *
// Returns a modified input char array where all uppercase letters are changed to lowercase.       *
//**************************************************************************************************
char *tolowercase(char *chars)
{
  char tmp;
  for (size_t i = 0; i < strlen(chars); ++i)
  {
    tmp = chars[i];
    if (tmp >= 0x41 && tmp <= 0x5a)
    {
      chars[i] = tmp + 0x20;
    }
  }
  return chars;
}

//**************************************************************************************************
//                                      G E T S D T R A C K S                                      *
//**************************************************************************************************
// Search all MP3 files on directory of SD card.                                                   *
//**************************************************************************************************
bool getsdtracks(const char *dirname, uint8_t levels)
{
  folderNum = 0;
  if (!listDirs(dirname, levels))
  {
    return false;
  }
  uint8_t i = 0;
  qsort(folders, folderNum, sizeof(char *), CompareStrings);
  while (i < folderNum)
  {
    if (listFiles(folders[i]))
    {
      if (fileNum > 0)
      {
        qsort(files, fileNum, sizeof(char *), CompareStrings);
        uint8_t j = 0;
        while (j < fileNum)
        {
          char *ext = files[j];                             // Point to begin of name
          char *ext2 = files[j];                            // Point to begin of name
          ext = ext + strlen(ext) - 4;                      // Point to extension type 1
          ext2 = ext2 + strlen(ext2) - 5;                   // Point to extension type 2
          if ((strcmp(tolowercase(ext), ".mp3") == 0) ||    // It is a file, but is it an MP3?
              (strcmp(tolowercase(ext), ".m4a") == 0) ||
              (strcmp(tolowercase(ext), ".ogg") == 0) ||
              (strcmp(tolowercase(ext), ".aac") == 0) ||
              (strcmp(tolowercase(ext2), ".flac") == 0))
          {
            sprintf(fullName, "%s/%s", folders[i], files[j]);
            if (!addToFileList(fullName)) // Add file to the list
            {
              break; // No need to continue
            }
          }
          j++;
          vTaskDelay(1 / portTICK_PERIOD_MS); // Allow others
        }
      }
    }
    i++;
  }
  return true;
}

//**************************************************************************************************
//                                       M O U N T _ S D C A R D                                   *
//**************************************************************************************************
// Initialize the SD card.                                                                         *
//**************************************************************************************************
bool mount_SDCARD()
{
  bool okay = false; // True if SD card in place and readable
  vTaskDelay(300 / portTICK_PERIOD_MS);       // Sometimes SD_MMC.begin() caused a reboot
  SD_mounted = SD_MMC.begin("/sdcard", true); // Yes, try to init SD card driver
  okay = (SD_MMC.cardType() != CARD_NONE); // See if known card
  if (!okay)
  {
    ESP_LOGW(STAG, "No SD card attached"); // Card not readable
  }
  return okay;
}

//**************************************************************************************************
//                                    C O U N T F I L E S                                          *
//**************************************************************************************************
// Count number of tracks in the track list.                                                       *
//**************************************************************************************************
int countfiles()
{
  int count = 0; // Files found

  while (trackfile.available() > 1)
  {
    uint8_t *pm = (uint8_t *)&mp3entry;            // Point to entrylength of mp3entry
    trackfile.read(pm, sizeof(mp3entry.entrylen)); // Get total size of entry
    pm += sizeof(mp3entry.entrylen);               // Bump pointer
    trackfile.read(pm, mp3entry.entrylen -         // Read rest of entry
                           sizeof(mp3entry.entrylen));
    count++;                             // count entries
    vTaskDelay(10 / portTICK_PERIOD_MS); // Allow other tasks
  }
  ESP_LOGW(STAG, "%d files on SD card", count);
  return count; // Return number of tracks
}

//**************************************************************************************************
//                                       S D T A S K                                               *
//**************************************************************************************************
// This task is used to create or read a tracklist file.                                           *
// if the SD detect pin is defined, interrupt is used to detect a change in the pin state.         *
// Otherwise, the check is made only once, after reset.                                            *
//**************************************************************************************************
void SDtask(void *parameter)
{
  const char *ffn;                       // First filename on SD card
  uint8_t dpin = config->sddpin;  // SD inserted detect pin
  vTaskDelay(3000 / portTICK_PERIOD_MS); // Start delay
  do
  {
    vTaskDelay(500 / portTICK_PERIOD_MS); // Allow other tasks

    if (sdin != oldsdin)
    {
      oldsdin = sdin;
      if (sdin) // New card is inserted !
      {
        ESP_LOGW(STAG, "SD card inserted");
        SD_okay = mount_SDCARD(); // Try to mount
        if (SD_okay)
        {
          SD_lastmp3spec[0] = '\0';   // No last track
          if ((openTrackfile()) &&    // Try to open trackfile
              (trackfile.size() > 0)) // Tracklist on this SD card?
          {
            ESP_LOGW(STAG, "Track list is on SD card, " // Yes, show it
                           "read tracks");
            SD_filecount = countfiles();           // Count number of files on this card
            closeTrackfile();                      // Close the tracklist file
            ESP_LOGW(STAG, "%d tracks on SD card", // Yes, show it
                     SD_filecount);
          }
          else
          {
            ESP_LOGW(STAG, "Locate mp3 files on SD, "
                           "may take a while...");
            if (openTrackfile(FILE_WRITE)) // Try to open trackfile for write
            {
              SD_filecount = 0;
              SD_okay = getsdtracks("/", SD_MAXDEPTH); // Get filenames, store on the SD card
              closeTrackfile();                        // Close the tracklist file
              ESP_LOGW(STAG, "Tracklist file creation complete. It contains %i tracks.", SD_filecount);
            }
          }
          if (openTrackfile()) // Try to open track file for FILE_READ
          {
            ffn = getSDFileName(0);
            if (ffn)
            {
              setSDFileName(ffn);
              ESP_LOGW(STAG, "First file on SD card is %s", // Show the first track name
                       SD_lastmp3spec);
            }
            sdready_req = true;
          }
        }
      }
      else // SD card removed !
      {
        ESP_LOGW(STAG, "SD card removed");
        if (SD_mounted) // Still mounted?
        {
          SD_MMC.end();       // Unmount SD card
          SD_okay = false;    // Not okay anymore
          SD_mounted = false; // And not mounted anymore
          sdready_req = true;
        }
      }
    }
  } while (dpin != 255);
  vTaskDelete(xsdtask);
}

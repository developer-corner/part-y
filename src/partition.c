/**
 * @file   partition.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions that manage
 *         MBRs or GPTs, respectively.
 *
 * [MIT license]
 *
 * Copyright (c) 2021 Ingo A. Kubbilun
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <part-y.h>

static const char fsTypeStrings[][32] =
{
  "*UNKNOWN*",
  "FAT12",
  "FAT16",
  "FAT32",
  "exFAT",
  "NTFS",
  "EXT2",
  "EXT3",
  "EXT4"
};

static const uint8_t zeros_16[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

static const struct
{
  uint8_t type_byte;
  char description[64];
} part_type_table_mbr[] =
{
  { 0x00, "empty" },
  { 0x01, "FAT12" },
  { 0x02, "XENIX root" },
  { 0x03, "XENIX usr" },
  { 0x04, "FAT16 < 32MB" },
  { 0x05, "Extended Partition" },
  { 0x06, "FAT16" },
  { 0x07, "HPFS/NTFS/exFAT" },
  { 0x08, "AIX" },
  { 0x09, "AIX bootable" },
  { 0x0A, "OS/2 Boot Manager" },
  { 0x0B, "WIN95 FAT32" },
  { 0x0C, "WIN95 FAT32 (LBA)" },
  { 0x0D, "Silicon Safe" },
  { 0x0E, "WIN95 FAT16 (LBA)" },
  { 0x0F, "WIN95 Extended Partition (LBA)" },
  { 0x10, "OPUS" },
  { 0x11, "Hidden FAT12" },
  { 0x12, "Compaq diagnostic partition" },
  { 0x14, "Hidden FAT16 < 32MB" },
  { 0x16, "Hidden FAT16" },
  { 0x17, "Hidden HPFS/NTFS" },
  { 0x18, "AST SmartSleep" },
  { 0x1b, "Hidden WIN95 FAT32" },
  { 0x1c, "Hidden WIN95 FAT32 (LBA)" },
  { 0x1e, "Hidden WIN95 FAT16 (LBA)" },
  { 0x24, "NEC DOS 3.x" },
  { 0x27, "Hidden NTFS Windows RE" },
  { 0x32, "NOS" },
  { 0x35, "JFS on OS/2" },
  { 0x38, "THEOS version 3.2 2GB" },
  { 0x39, "Plan 9 / THEOS" },
  { 0x3A, "THEOS version 4 4GB" },
  { 0x3B, "THEOS version 4 extended partition" },
  { 0x3c, "PartitionMagic recovery partition" },
  { 0x3d, "Hidden Netware" },
  { 0x40, "Venix 80286" },
  { 0x41, "PPC PReP(Power PC Reference Platform) Boot" },
  { 0x42, "SFS (secure filesystem)" },
  { 0x44, "GoBack partition" },
  { 0x45, "Boot - US boot manager / Priam / EUMEL" },
  { 0x46, "EUMEL / Elan" },
  { 0x47, "EUMEL / Elan" },
  { 0x48, "EUMEL / Elan" },
  { 0x4c, "Oberon partition" },
  { 0x4d, "QNX4.x" },
  { 0x4e, "QNX4.x 2nd part" },
  { 0x4f, "QNX4.x 3rd part / Oberon" },
  { 0x50, "OnTrack Disk Manager" },
  { 0x51, "OnTrack Disk Manager DM6 Aux" },
  { 0x52, "CP/M" },
  { 0x53, "OnTrack Disk Manager DM6 Aux3" },
  { 0x54, "OnTrack Disk Manager DM6 Dynamic Drive Overlay (DDO)" },
  { 0x55, "EZ - Drive" },
  { 0x56, "Golden Bow" },
  { 0x5c, "Priam Edisk" },
  { 0x61, "SpeedStor" },
  { 0x63, "GNU HURD / UNIX System V / Mach" },
  { 0x64, "Novell Netware 286" },
  { 0x65, "Novell Netware 386" },
  { 0x66, "Novell Netware SMS Partition" },
  { 0x67, "Novell" },
  { 0x68, "Novell" },
  { 0x69, "Novell Netware 5+ , Novell Netware NSS Partition" },
  { 0x70, "DiskSecure Mult" },
  { 0x75, "IBM PC / IX" },
  { 0x77, "VNDI" },
  { 0x80, "Old Minix" },
  { 0x81, "Minix / old Linux" },
  { 0x82, "Linux swap / Solaris x86" },
  { 0x83, "Linux (native partition)" },
  { 0x84, "OS/2 hidden C: drive / hibernation partition" },
  { 0x85, "Linux extended partition" },
  { 0x86, "FAT16 volume set" },
  { 0x87, "NTFS volume set" },
  { 0x88, "Linux plaintext partition table" },
  { 0x8e, "Linux LVM (Logical Volume Manager)" },
  { 0x93, "Amoeba / Hidden Linux native partition" },
  { 0x94, "Amoeba BBT (Bad Block Table)" },
  { 0x9f, "BSD / OS" },
  { 0xa0, "IBM Thinkpad hibernation partition" },
  { 0xa1, "hibernation partition" },
  { 0xa5, "FreeBSD / NetBSD" },
  { 0xa6, "OpenBSD" },
  { 0xa7, "NeXTSTEP" },
  { 0xa8, "Darwin UFS (MacOS)" },
  { 0xa9, "NetBSD" },
  { 0xab, "Darwin boot partition (MacOS)" },
  { 0xaf, "HFS/HFS+ (MacOS)" },
  { 0xb7, "BSDI BSD/386 filesystem" },
  { 0xb8, "BSDI BSD/386 swap partition" },
  { 0xbb, "Boot Wizard hidden partition" },
  { 0xbc, "Acronis FAT32 backup partition" },
  { 0xbe, "Solaris 8 boot partition" },
  { 0xbf, "Solaris x86 partition" },
  { 0xc0, "CTOS" },
  { 0xc1, "DRDOS - secured FAT12" },
  { 0xc2, "Hidden Linux" },
  { 0xc3, "Hidden Linux swap partition" },
  { 0xc4, "DRDOS - secured FAT16 < 32MB" },
  { 0xc5, "DRDOS - secured (extended)" },
  { 0xc6, "DRDOS - secured FAT16 >= 32MB" },
  { 0xc7, "Syrinx boot" },
  { 0xcb, "DRDOS - secured FAT32(CHS)" },
  { 0xcc, "DRDOS - secured FAT32(LBA)" },
  { 0xce, "DRDOS - FAT16X(LBA)" },
  { 0xcf, "DRDOS - secured EXT DOS(LBA)" },
  { 0xd8, "CP/M-86" },
  { 0xda, "Non - FS data" },
  { 0xdb, "CP/M / CTOS" },
  { 0xde, "Dell Utility" },
  { 0xdf, "BootIt" },
  { 0xe1, "DOS access" },
  { 0xe3, "DOS R/O" },
  { 0xe4, "SpeedStor 16bit FAT extended partition < 1024 cyl." },
  { 0xe8, "LUKS (Linux Unified Key Setup)" },
  { 0xea, "Rufus alignment" },
  { 0xeb, "BeOS BFS" },
  { 0xee, "GPT (MBR followed by EFI header)" },
  { 0xef, "EFI" },
  { 0xf0, "Linux / PA-RISC boot loader" },
  { 0xf1, "SpeedStor" },
  { 0xf4, "SpeedStor (large partition)" },
  { 0xf2, "DOS secondary" },
  { 0xf6, "Speedstor" },
  { 0xfb, "VMware VMFS" },
  { 0xfc, "VMware VMKCORE / swap" },
  { 0xfd, "Linux raid auto" },
  { 0xfe, "LANstep" },
  { 0xff, "Bad Block Table (BBT) / Xenix" }
};

static const struct
{
  char          guid_str[40];
  uint8_t       mbr_type;
  char          mbr_description[64];
  char          gpt_description[64];
} part_type_table_gpt[] =
{
  { "00000000-0000-0000-0000-000000000000", 0x00, "empty", "unused entry" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x01, "FAT12", "Microsoft basic data"},
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x04, "FAT16 < 32MB", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x06, "FAT16", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x07, "HPFS/NTFS/exFAT", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0B, "FAT32", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0C, "FAT32 (LBA)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0E, "FAT16 (LBA)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x11, "FAT12 (hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x14, "FAT16 < 32MB (hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x16, "FAT16 (hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x17, "HPFS/NTFS/exFAT (hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1B, "FAT32 (hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1C, "FAT32 (LBA, hidden)", "Microsoft basic data" },
  { "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1E, "FAT16 (LBA, hidden)", "Microsoft basic data" },
  { "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", 0x0C, "Hybrid-MBR", "Microsoft reserved"}, // equals FAT32, see above
  { "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", 0x27, "Windows RE", "Windows RE" },
  { "7412F7D5-A156-4B13-81DC-867174929325", 0x30, "ONIE (Open Network Install Environment)", "ONIE boot" },
  { "D4E6E2CD-4469-46F3-B5CB-1BFF57AFC149", 0xE1, "ONIE (Open Network Install Environment)", "ONIE config" },
  { "C91818F9-8025-47AF-89D2-F030D7000C2C", 0x39, "Plan 9", "Plan 9" },
  { "9E1A2D38-C612-4316-AA26-8B49521E5A8B", 0x41, "PReP", "PowerPC PReP boot" },
  { "AF9B60A0-1431-4F62-BC68-3311714A69AD", 0x42, "Windows", "Windows LDM data" },
  { "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", 0x42, "Windows", "Windows LDM metadata" },
  { "E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", 0x42, "Windows", "Windows Storage Spaces" },
  { "37AFFC90-EF7D-4E96-91C3-2D7AE055B174", 0x75, "IBM GPFS", "IBM GPFS" },
  { "FE3A2A5D-4F32-41A7-B725-ACCC3285A309", 0x7F, "Chromebook", "ChromeOS kernel" },
  { "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", 0x7F, "Chromebook", "ChromeOS root" },
  { "2E0A753D-9E48-43B0-8337-B15192CB1B5E", 0x7F, "Chromebook", "ChromeOS reserved" },
  { "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", 0x82, "Linux swap", "Linux swap" },
  { "0FC63DAF-8483-4772-8E79-3D69D8477DE4", 0x83, "Linux native", "Linux filesystem" },
  { "8DA63339-0007-60C0-C436-083AC8230908", 0x83, "Linux native", "Linux reserved" },
  { "933AC7E1-2EB4-4F13-B844-0E14E2AEF915", 0x83, "freedesktop.org (Linux)", "Linux /home" },
  { "3B8F8425-20E0-4F3B-907F-1A25A76F98E8", 0x83, "freedesktop.org (Linux)", "Linux /srv" },
  { "7FFEC5C9-2D00-49B7-8941-3EA10A5586B7", 0x83, "freedesktop.org (Linux)", "Linux dm-crypt" },
  { "CA7D7CCB-63ED-4C53-861C-1742536059CC", 0x83, "freedesktop.org (Linux)", "Linux LUKS" },
  { "44479540-F297-41B2-9AF7-D131D5F0458A", 0x83, "freedesktop.org (Linux)", "root partition / Linux x86 (x86/32bit platform)" },
  { "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", 0x83, "freedesktop.org (Linux)", "root partition / Linux x86-64 (AMD64 platform)" },
  { "69DAD710-2CE4-4E3C-B16C-21A1D49ABED3", 0x83, "freedesktop.org (Linux)", "root partition / Linux ARM32 platform" },
  { "B921B045-1DF0-41C3-AF44-4C6F280D3FAE", 0x83, "freedesktop.org (Linux)", "root partition / Linux ARM64 platform" },
  { "993d8d3d-f80e-4225-855a-9daf8ed7ea97", 0x00, "freedesktop.org (Linux)", "root partition / Linux IA64 platform" }, // MBR-Type 0x00 means: there is NO MBR type
  { "D3BFE2DE-3DAF-11DF-BA40-E3A556D89593", 0x84, "Intel-PC", "Intel Rapid Start" },
  { "E6D6D379-F507-44C2-A23C-238F2A3DF928", 0x8E, "Linux LVM", "Linux LVM" },
  { "734E5AFE-F61A-11E6-BC64-92361F002671", 0xA2, "Atari TOS", "TOS basic data" },
  { "516E7CB4-6ECF-11D6-8FF8-00022D09712B", 0xA5, "FreeBSD", "FreeBSD Disklabel" },
  { "83BD6B9D-7F41-11DC-BE0B-001560B84F0F", 0xA5, "FreeBSD", "FreeBSD boot" },
  { "516E7CB5-6ECF-11D6-8FF8-00022D09712B", 0xA5, "FreeBSD", "FreeBSD swap" },
  { "516E7CB6-6ECF-11D6-8FF8-00022D09712B", 0xA5, "FreeBSD", "FreeBSD UFS" },
  { "516E7CBA-6ECF-11D6-8FF8-00022D09712B", 0xA5, "FreeBSD", "FreeBSD ZFS" },
  { "516E7CB8-6ECF-11D6-8FF8-00022D09712B", 0xA5, "FreeBSD", "FreeBSD Vinum/RAID" },
  { "85D5E45A-237C-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD data" },
  { "85D5E45E-237C-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD boot" },
  { "85D5E45B-237C-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD swap" },
  { "0394EF8B-237E-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD UFS" },
  { "85D5E45D-237C-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD ZFS" },
  { "85D5E45C-237C-11E1-B4B3-E89A8F7FC3A7", 0xA5, "MidnightBSD", "MidnightBSD Vinum" },
  { "824CC7A0-36A8-11E3-890A-952519AD3F61", 0xA6, "OpenBSD", "OpenBSD data" },
  { "55465300-0000-11AA-AA11-00306543ECAC", 0xA8, "Mac OS X", "Apple UFS" },
  { "516E7CB4-6ECF-11D6-8FF8-00022D09712B", 0xA9, "FreeBSD", "FreeBSD Disklabel" },
  { "49F48D32-B10E-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD swap" },
  { "49F48D5A-B10E-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD FFS" },
  { "49F48D82-B10E-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD LFS" },
  { "2DB519C4-B10F-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD concatenated" },
  { "2DB519EC-B10F-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD encrypted" },
  { "49F48DAA-B10E-11DC-B99B-0019D1879648", 0xA9, "NetBSD", "NetBSD RAID" },
  { "426F6F74-0000-11AA-AA11-00306543ECAC", 0xAB, "macOS", "Apple boot" },
  { "48465300-0000-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple HFS/HFS+" },
  { "52414944-0000-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple RAID" },
  { "52414944-5F4F-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple RAID offline" },
  { "4C616265-6C00-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple Label" },
  { "5265636F-7665-11AA-AA11-00306543ECAC", 0xAF, "macOS", "AppleTV Recovery" },
  { "53746F72-6167-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple Core Storage" },
  { "B6FA30DA-92D2-4A9A-96F1-871EC6486200", 0xAF, "macOS", "Apple SoftRAID Status" },
  { "2E313465-19B9-463F-8126-8A7993773801", 0xAF, "macOS", "Apple SoftRAID Scratch" },
  { "FA709C7E-65B1-4593-BFD5-E71D61DE9B02", 0xAF, "macOS", "Apple SoftRAID Volume" },
  { "BBBA6DF5-F46F-4A89-8F59-8765B2727503", 0xAF, "macOS", "Apple SoftRAID Cache" },
  { "7C3457EF-0000-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple APFS" },
  { "CEF5A9AD-73BC-4601-89F3-CDEEEEE321A1", 0xB3, "QNX", "QNX6 Power-Safe" },
  { "0311FC50-01CA-4725-AD77-9ADBB20ACE98", 0xBC, "Acronis", "Acronis Secure Zone" },
  { "6A82CB45-1DD2-11B2-99A6-080020736631", 0xBE, "Solaris", "Solaris boot" },
  { "6A85CF4D-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris root" },
  { "6A898CC3-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris /usr" },
  { "6A87C46F-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris swap" },
  { "6A8B642B-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris backup" },
  { "6A8EF2E9-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris /var" },
  { "6A90BA39-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris /home" },
  { "6A9283A5-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris alternate sector" },
  { "6A945A3B-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris Reserved" },
  { "6A9630D1-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris Reserved" },
  { "6A980767-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris Reserved" },
  { "6A96237F-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris Reserved" },
  { "6A8D2AC7-1DD2-11B2-99A6-080020736631", 0xBF, "Solaris", "Solaris Reserved" },
  { "75894C1E-3AEB-11D3-B7C1-7B03A0000000", 0xC0, "HP-UX", "HP-UX data" },
  { "E2A1E728-32E3-11D6-A682-7B03A0000000", 0xC0, "HP-UX", "HP-UX service" },
  { "BC13C2FF-59E6-4262-A352-B275FD6F7172", 0xEA, "freedesktop.org", "Freedesktop $BOOT" },
  { "42465331-3BA3-10F1-802A-4861696B7521", 0xEB, "Haiku", "Haiku BFS" },
  { "BFBFAFE7-A34F-448A-9A5B-6213EB736C22", 0xED, "ESP (OEM-specific)", "Lenovo system partition" },
  { "F4019732-066E-4E12-8273-346C5641494F", 0xED, "ESP (OEM-specific)", "Sony system partition" },
  { "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 0xEF, "EFI", "EFI System Partition (ESP)" },
  { "024DEE41-33E7-11D3-9D69-0008C781F39F", 0xEF, "EFI", "MBR partition scheme" }, // we can use this type to encapsulate an MBR in it
  { "21686148-6449-6E6F-744E-656564454649", 0xEF, "EFI", "BIOS boot partition" }, // because no MBR-gap on GPT-disks, this is the UUID for the e.g. GRUB2 boot loader
  { "4FBD7E29-9D25-41B8-AFD0-062C0CEFF05D", 0xF8, "Ceph", "Ceph OSD" },
  { "4FBD7E29-9D25-41B8-AFD0-5EC00CEFF05D", 0xF8, "Ceph", "Ceph dm-crypt OSD" },
  { "45B0969E-9B03-4F30-B4C6-B4B80CEFF106", 0xF8, "Ceph", "Ceph journal" },
  { "45B0969E-9B03-4F30-B4C6-5EC00CEFF106", 0xF8, "Ceph", "Ceph dm-crypt journal" },
  { "89C57F98-2FE5-4DC0-89C1-F3AD0CEFF2BE", 0xF8, "Ceph", "Ceph disk in creation" },
  { "89C57F98-2FE5-4DC0-89C1-5EC00CEFF2BE", 0xF8, "Ceph", "Ceph dm-crypt disk in creation" },
  { "AA31E02A-400F-11DB-9590-000C2911D1B8", 0xFB, "VMWare ESX", "VMware VMFS" },
  { "9198EFFC-31C0-11DB-8F78-000C2911D1B8", 0xFB, "VMWare ESX", "VMware reserved" },
  { "9D275380-40AD-11DB-BF97-000C2911D1B8", 0xFC, "VMWare ESX", "VMware kcore crash protection" },
  { "A19D880F-05FC-4D3B-A006-743F0F84911E", 0xFD, "Linux", "Linux RAID" }
};

static struct
{
  uint64_t      attributes;
  char          guid_str[40];
  uint8_t       mbr_type;
  char          mbr_description[64];
  char          gpt_description[64];
} part_convert_table_gpt[] =
{
  { 0, "00000000-0000-0000-0000-000000000000", 0x00, "empty", "unused entry" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x01, "FAT12", "Microsoft basic data"},
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x04, "FAT16 < 32MB", "Microsoft basic data" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x06, "FAT16", "Microsoft basic data" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x07, "HPFS/NTFS/exFAT", "Microsoft basic data" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0B, "FAT32", "Microsoft basic data" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0C, "FAT32 (LBA)", "Microsoft basic data" },
  { 0, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x0E, "FAT16 (LBA)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x11, "FAT12 (hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x14, "FAT16 < 32MB (hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x16, "FAT16 (hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x17, "HPFS/NTFS/exFAT (hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1B, "FAT32 (hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1C, "FAT32 (LBA, hidden)", "Microsoft basic data" },
  { GPT_ATTR_HIDDEN, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 0x1E, "FAT16 (LBA, hidden)", "Microsoft basic data" },
  { GPT_ATTR_DO_NOT_MOUNT, "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", 0x27, "Windows RE", "Windows RE" },
  { 0, "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", 0x82, "Linux swap", "Linux swap" },
  { 0, "0FC63DAF-8483-4772-8E79-3D69D8477DE4", 0x83, "Linux native", "Linux filesystem" },
  { 0, "E6D6D379-F507-44C2-A23C-238F2A3DF928", 0x8E, "Linux LVM", "Linux LVM" },
  { 0, "55465300-0000-11AA-AA11-00306543ECAC", 0xA8, "Mac OS X", "Apple UFS" },
  { 0, "426F6F74-0000-11AA-AA11-00306543ECAC", 0xAB, "macOS", "Apple boot" },
  { 0, "48465300-0000-11AA-AA11-00306543ECAC", 0xAF, "macOS", "Apple HFS/HFS+" },
  { GPT_ATTR_DO_NOT_MOUNT, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 0xEF, "EFI", "EFI System Partition (ESP)" },
  { 0, "A19D880F-05FC-4D3B-A006-743F0F84911E", 0xFD, "Linux", "Linux RAID" }
};

bool gpt_get_guid_for_mbr_type(uint8_t part_type, uint8_t* guid, uint64_t *attributes)
{
  uint32_t            i;

  for (i = 0; i < sizeof(part_convert_table_gpt) / sizeof(part_convert_table_gpt[0]); i++)
  {
    if (part_type == part_convert_table_gpt[i].mbr_type)
    {
      parse_guid(guid, part_convert_table_gpt[i].guid_str, false /* parse it with mixed endianess */);
      *attributes = part_convert_table_gpt[i].attributes;
      return true;
    }
  }

  return false;
}

// additional lba offset is required for extended partition tables because everything is specified relative to the LBA of the extended
// partition table itself
static bool mbr_parse_part_entry(disk_ptr dp, DISK_HANDLE h, const uint8_t* data, mbr_entry_ptr mep, uint64_t additional_lba_offset )
{
  size_t          i;

  mep->boot_flag = data[0x00];
  if (0x00 != mep->boot_flag && 0x80 != mep->boot_flag)
    return false;

  mep->part_type = data[0x04];

  mep->head_first = ((uint32_t)data[0x01]);
  mep->sector_first = ((uint32_t)data[0x02]) & 0x3F; // 0x00 not allowed
  mep->cylinder_first = ((uint32_t)data[0x03]) | ((((uint32_t)data[0x02]) & 0xC0) << 2);

  mep->head_last = ((uint32_t)data[0x05]);
  mep->sector_last = ((uint32_t)data[0x06]) & 0x3F; // 0x00 not allowed
  mep->cylinder_last = ((uint32_t)data[0x07]) | ((((uint32_t)data[0x06]) & 0xC0) << 2);

  mep->start_sector = READ_LITTLE_ENDIAN32(data, 0x08);
  mep->num_sectors = READ_LITTLE_ENDIAN32(data, 0x0C);

  for (i = 0; i < sizeof(part_type_table_mbr) / sizeof(part_type_table_mbr[0]); i++)
  {
    if (part_type_table_mbr[i].type_byte == mep->part_type)
    {
      strncpy(mep->type_desc, part_type_table_mbr[i].description, sizeof(mep->type_desc) - 1);

      // check if we should 'peek' into the file system

      switch (mep->part_type)
      {
        case 0x01: // FAT12
        case 0x11:
          mep->fs_type = FSYS_WIN_FAT12;
          break;
        case 0x04: // FAT16
        case 0x06:
        case 0x14:
        case 0x16:
          mep->fs_type = FSYS_WIN_FAT16;
          break;
        case 0x0B: // FAT32
        case 0x0C:
        case 0x0E:
        case 0x1B:
        case 0x1C:
        case 0x1E:
          mep->fs_type = FSYS_WIN_FAT32;
          break;
        case 0x07:
        case 0x17:
        case 0x27:
        case 0x83:
        case 0xC2: // peek file system and try to find out what is in there
          mep->fs_type = partition_peek_filesystem(dp, h, mep->start_sector + additional_lba_offset, mep->uuid);
          break;
      }

      break;
    }
  }

  if (i == sizeof(part_type_table_mbr) / sizeof(part_type_table_mbr[0]))
    strncpy(mep->type_desc, "*UNKNOWN*", sizeof(mep->type_desc) - 1);

  return true;
}

static mbr_part_sector_ptr mbr_parse_boot_sector(disk_ptr dp, DISK_HANDLE h, sector_ptr sp)
{
  mbr_part_sector_ptr       mbsp = (mbr_part_sector_ptr)malloc(sizeof(mbr_part_sector));

  if (unlikely(NULL == mbsp))
    return NULL;

  memset(mbsp, 0, sizeof(mbr_part_sector));

  mbsp->sp = sp;

  mbsp->disk_signature = READ_LITTLE_ENDIAN32(sp->data, 0x01B8);

  mbsp->boot_sector_signature1 = sp->data[0x01FE]; // 0x55
  mbsp->boot_sector_signature2 = sp->data[0x01FF]; // 0xAA
  mbsp->ext_part_no = 0xFF; // no extended partition

  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01BE], &mbsp->part_table[0], sp->lba))
  {
ErrorExit:
    free(mbsp);
    return NULL;
  }
  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01CE], &mbsp->part_table[1], sp->lba))
    goto ErrorExit;
  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01DE], &mbsp->part_table[2], sp->lba))
    goto ErrorExit;
  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01EE], &mbsp->part_table[3], sp->lba))
    goto ErrorExit;

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[0].part_type))
    mbsp->ext_part_no = 0;

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[1].part_type))
  {
    if (0xFF != mbsp->ext_part_no)
      goto ErrorExit; // only one ext. part. allowed!
    mbsp->ext_part_no = 1;
  }

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[2].part_type))
  {
    if (0xFF != mbsp->ext_part_no)
      goto ErrorExit; // only one ext. part. allowed!
    mbsp->ext_part_no = 2;
  }

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[3].part_type))
  {
    if (0xFF != mbsp->ext_part_no)
      goto ErrorExit; // only one ext. part. allowed!
    mbsp->ext_part_no = 3;
  }

  if (0x55 == mbsp->boot_sector_signature1 && 0xAA == mbsp->boot_sector_signature2)
    return mbsp;

  goto ErrorExit;
}

static mbr_part_sector_ptr mbr_parse_ext_part_sector(disk_ptr dp, DISK_HANDLE h, sector_ptr sp)
{
  mbr_part_sector_ptr       mbsp = (mbr_part_sector_ptr)malloc(sizeof(mbr_part_sector));

  if (unlikely(NULL == mbsp))
    return NULL;

  memset(mbsp, 0, sizeof(mbr_part_sector));

  mbsp->sp = sp;

  mbsp->boot_sector_signature1 = sp->data[0x01FE]; // 0x55
  mbsp->boot_sector_signature2 = sp->data[0x01FF]; // 0xAA
  mbsp->ext_part_no = 0xFF; // no extended partition
  
  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01BE], &mbsp->part_table[0],sp->lba))
  {
ErrorExit:
    free(mbsp);
    return NULL;
  }
  if (!mbr_parse_part_entry(dp, h, &sp->data[0x01CE], &mbsp->part_table[1],sp->lba))
    goto ErrorExit;
  if (0 != memcmp(&sp->data[0x01DE], zeros_16, 16))
    goto ErrorExit;
  if (0 != memcmp(&sp->data[0x01EE], zeros_16, 16))
    goto ErrorExit;

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[0].part_type))
    goto ErrorExit;

  if (0 == memcmp(&sp->data[0x01BE], zeros_16, 16))
    goto ErrorExit;

  mbsp->part_table[0].start_sector += sp->lba;

  if (MBR_IS_EXTENDED_PARTITION(mbsp->part_table[1].part_type))
  {
    mbsp->ext_part_no = 1;
    mbsp->part_table[1].start_sector += sp->lba;
  }
  else
  {
    if (0 != memcmp(&sp->data[0x01CE], zeros_16, 16))
      goto ErrorExit;
  }

  if (0x55 == mbsp->boot_sector_signature1 && 0xAA == mbsp->boot_sector_signature2)
    return mbsp;

  goto ErrorExit;
}

mbr_part_sector_ptr partition_scan_mbr(disk_ptr dp, DISK_HANDLE h)
{
  mbr_part_sector_ptr           head = NULL;
  mbr_part_sector_ptr           tail = NULL;
  mbr_part_sector_ptr           item;
  sector_ptr                    sp;

  // read LBA 0, i.e. the MBR
  
  sp = disk_read_sectors(dp, h, NULL, NULL, 0/*LBA*/, 1);
  if (NULL == sp)
    return NULL;

  item = mbr_parse_boot_sector(dp, h, sp);
  if (NULL == item)
  {
    disk_free_sector(sp);
    return NULL;
  }

  // check for protective MBR

  if ((0xEE == item->part_table[0].part_type) &&
      (0x00 == item->part_table[1].part_type) &&
      (0x00 == item->part_table[2].part_type) &&
      (0x00 == item->part_table[3].part_type))
    dp->flags |= DISK_FLAG_MBR_IS_PROTECTIVE;

  head = tail = item;

  // check for extended partitions
  
  while (0xFF != item->ext_part_no)
  {
    sp = disk_read_sectors(dp, h, NULL, NULL, item->part_table[item->ext_part_no].start_sector, 1); // sp can be overwritten here because it is linked in the last item pointer!
    if (NULL == sp)
    {
      partition_free_mbr_part_sector_list(head);
      return NULL;
    }

    item = mbr_parse_ext_part_sector(dp, h, sp);
    if (NULL == item)
    {
      disk_free_sector(sp); // because here, sp was not yet linked to the last item pointer!
      partition_free_mbr_part_sector_list(head);
      return NULL;
    }

    item->prev = tail;
    // if (NULL == tail)    -> not possible because MBR there (see above)
    //   head = item;       -> not possible because MBR there (see above)
    // else                 -> not possible because MBR there (see above)
      tail->next = item;
    tail = item;
  }

  return head;
}

void partition_free_mbr_part_sector_list(mbr_part_sector_ptr item)
{
  mbr_part_sector_ptr             next;

  while (NULL != item)
  {
    next = item->next;
    disk_free_sector(item->sp);
    free(item);
    item = next;
  }
}

static uint32_t calc_crc32(const uint8_t* buf, uint32_t len, uint32_t init /* typically 0xFFFFFFFF */)
{
  uint32_t i, crc = init, j;

  for (i = 0; i < len; i++)
  {
    crc ^= (uint32_t)buf[i];
    for (j = 0; j < 8; j++)
    {
      if (1 == (crc & 1))
      {
        crc >>= 1;
        crc ^= 0xedb88320;
      }
      else
        crc >>= 1;
    }
  }

  return ~crc;
}

static bool gpt_read_and_parse_entries(disk_ptr dp, DISK_HANDLE h, gpt_ptr gptp)
{
  uint32_t        entry_sectors = ((gptp->header.number_of_part_entries * gptp->header.size_of_part_entry) + SECTOR_SIZE_MASK) >> SECTOR_SHIFT;
  sector_ptr      sp = disk_read_sectors(dp, h, NULL, NULL, gptp->header.starting_lba_part_entries, entry_sectors);
  uint32_t        i, crc32;
  uint8_t        *raw_gpt_entry;

  if (NULL == sp)
    return false;

  gptp->sp = sp;

  for (i = 0; i < gptp->header.number_of_part_entries; i++)
  {
    raw_gpt_entry = sp->data + i * gptp->header.size_of_part_entry;

    memcpy(gptp->entries[i].type_guid, raw_gpt_entry + 0x0000, 16);
    memcpy(gptp->entries[i].partition_guid, raw_gpt_entry + 0x0010, 16);

    gptp->entries[i].part_start_lba = READ_LITTLE_ENDIAN64(raw_gpt_entry, 0x0020);
    gptp->entries[i].part_end_lba = READ_LITTLE_ENDIAN64(raw_gpt_entry, 0x0028);

    gptp->entries[i].attributes = READ_LITTLE_ENDIAN64(raw_gpt_entry, 0x0030);

    memcpy(gptp->entries[i].part_name, raw_gpt_entry + 0x0038, 72);

    (void)convertUTF162UTF8(gptp->entries[i].part_name, (uint8_t*)gptp->entries[i].part_name_utf8_oem, sizeof(gptp->entries[i].part_name_utf8_oem), true);
  }

  crc32 = calc_crc32(sp->data, gptp->header.number_of_part_entries * gptp->header.size_of_part_entry, 0xFFFFFFFF);

  if (crc32 != gptp->header.part_entries_crc32)
    gptp->header.entries_corrupt = true;

  return true;
}

bool partition_compare_gpts(gpt_ptr g1, gpt_ptr g2)
{
  uint32_t                i;

  // 1. compare the headers

  if (g1->header.revision != g2->header.revision)
    return false;

  if (g1->header.header_size != g2->header.header_size)
    return false;

  if (g1->header.current_lba != g2->header.backup_lba)
    return false;

  if (g1->header.backup_lba != g2->header.current_lba)
    return false;

  if (g1->header.first_usable_lba != g2->header.first_usable_lba)
    return false;

  if (g1->header.last_usable_lba != g2->header.last_usable_lba)
    return false;

  if (memcmp(g1->header.disk_guid, g2->header.disk_guid, 16))
    return false;

  if (g1->header.number_of_part_entries != g2->header.number_of_part_entries)
    return false;

  if (g1->header.size_of_part_entry != g2->header.size_of_part_entry)
    return false;

  if (g1->header.part_entries_crc32 != g2->header.part_entries_crc32)
    return false;

  for (i = 0; i < g1->header.number_of_part_entries; i++)
    if (memcmp(&g1->entries[i], &g2->entries[i], sizeof(gpt_entry)))
      return false;

  return true;
}

static gpt_ptr gpt_parse_header(sector_ptr sp)
{
  uint32_t              i, crc32, orig_crc32;
  gpt_ptr               gptp;

  gptp = (gpt_ptr)malloc(sizeof(gpt));
  if (unlikely(NULL == gptp))
    return NULL;

  memset(gptp, 0, sizeof(gpt));

  gptp->header.sp = sp;

  if (memcmp(&sp->data[0x0000], "EFI PART", 8))
  {
ErrorExit:
    free(gptp);
    return NULL;
  }

  gptp->header.revision = READ_LITTLE_ENDIAN32(sp->data, 0x0008);
  gptp->header.header_size = READ_LITTLE_ENDIAN32(sp->data, 0x000C);
  if (0x5C != gptp->header.header_size)
    goto ErrorExit;

  gptp->header.header_crc32 = READ_LITTLE_ENDIAN32(sp->data, 0x0010);

  if (0 != READ_LITTLE_ENDIAN32(sp->data, 0x0014))
    goto ErrorExit;

  gptp->header.current_lba = READ_LITTLE_ENDIAN64(sp->data, 0x0018);
  if (gptp->header.current_lba != sp->lba)
    goto ErrorExit;

  gptp->header.backup_lba = READ_LITTLE_ENDIAN64(sp->data, 0x0020);
  if (1 != sp->lba && 1 != gptp->header.backup_lba)
    goto ErrorExit;

  gptp->header.first_usable_lba = READ_LITTLE_ENDIAN64(sp->data, 0x0028);
  gptp->header.last_usable_lba = READ_LITTLE_ENDIAN64(sp->data, 0x0030);

  memcpy(gptp->header.disk_guid, &sp->data[0x0038], 16);

  gptp->header.starting_lba_part_entries = READ_LITTLE_ENDIAN64(sp->data, 0x0048); // 2 in primary copy
  if (1 == sp->lba && 2 != gptp->header.starting_lba_part_entries)
    goto ErrorExit;

  gptp->header.number_of_part_entries = READ_LITTLE_ENDIAN32(sp->data, 0x0050);
  if (gptp->header.number_of_part_entries > 0x80) // not supported
    goto ErrorExit;

  gptp->header.size_of_part_entry = READ_LITTLE_ENDIAN32(sp->data, 0x0054);
  if (0x80 != gptp->header.size_of_part_entry)
    goto ErrorExit;

  gptp->header.part_entries_crc32 = READ_LITTLE_ENDIAN32(sp->data, 0x0058);

  for (i = 0x5C; i < 0x200; i++)
    if (0x00 != sp->data[i])
      break;

  if (0x200 != i)
    goto ErrorExit;

  // temporarily zero-out crc32

  orig_crc32 = *((uint32_t*)(sp->data + 0x10));
  *((uint32_t*)(sp->data + 0x10)) = 0;

  // compute CRC32

  crc32 = calc_crc32(sp->data, 0x5C, 0xFFFFFFFF);

  // restore original one in sector data (in memory)

  *((uint32_t*)(sp->data + 0x10)) = orig_crc32;

  gptp->header.header_corrupt = (crc32 != gptp->header.header_crc32) ? true : false;

  return gptp;
}

void partition_free_gpt(gpt_ptr gptp)
{
  if (NULL != gptp)
  {
    if (NULL != gptp->sp)
      disk_free_sector(gptp->sp);
    if (NULL != gptp->header.sp)
      disk_free_sector(gptp->header.sp);
    free(gptp);
  }
}

gpt_ptr partition_scan_gpt(disk_ptr dp, DISK_HANDLE h, uint64_t lba)
{
  sector_ptr                    sp_header;
  gpt_ptr                       gpt;

  sp_header = disk_read_sectors(dp, h, NULL, NULL, lba, 1);
  if (NULL == sp_header)
    return NULL;

  gpt = gpt_parse_header(sp_header);
  if (NULL == gpt)
  {
    disk_free_sector(sp_header);
    return NULL;
  }

  sp_header = NULL; // now part of structure pointed to by gpt

  if (!gpt_read_and_parse_entries(dp, h, gpt))
  {
    partition_free_gpt(gpt);
    return NULL;
  }

  return gpt;
}

bool partition_peek_fs_for_gpt(disk_ptr dp, DISK_HANDLE h)
{
  gpt_ptr             g;
  uint32_t            i;
  char                current_guid[48];

  if (unlikely(NULL == dp))
    return false;

  g = dp->primary_gpt_corrupt ? NULL : dp->gpt1;
  if (NULL == g)
    g = dp->backup_gpt_corrupt ? NULL : dp->gpt2;

  if (NULL == g)
    return false;

  for (i = 0; i < g->header.number_of_part_entries; i++)
  {
    format_guid(current_guid, g->entries[i].type_guid, false/*use mixed endian*/);

    if ((!memcmp(current_guid, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", 36)) || // Microsoft basic data
      (!memcmp(current_guid, "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", 36)) || // Hybrid-MBR | Microsoft reserved
      (!memcmp(current_guid, "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", 36)) || // Windows Recovery Environment (RE)
      (!memcmp(current_guid, "0FC63DAF-8483-4772-8E79-3D69D8477DE4", 36)) || // Linux native
      (!memcmp(current_guid, "8DA63339-0007-60C0-C436-083AC8230908", 36)) || // Linux reserved
      (!memcmp(current_guid, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 36)) || // EFI System Partition (ESP)
      (!memcmp(current_guid, "024DEE41-33E7-11D3-9D69-0008C781F39F", 36)) || // EFI MBR partition scheme
      (!memcmp(current_guid, "21686148-6449-6E6F-744E-656564454649", 36))  // EFI BIOS boot partition
      )
    {
      g->entries[i].fs_type = partition_peek_filesystem(dp, h, g->entries[i].part_start_lba, g->entries[i].fs_uuid);
    }
  }

  return true;
}

disk_map_ptr partition_create_disk_map_mbr(disk_ptr dp)
{
  disk_map_ptr          head = NULL, tail = NULL;
  disk_map_ptr          dmp;
  uint32_t              i;
  mbr_part_sector_ptr   mpsp = dp->mbr;
  bool                  is_mbr;

  while (NULL != mpsp)
  {
    is_mbr = (0 == mpsp->sp->lba) ? true : false;

    // add the MBR or the extended partition sector itself
    
    dmp = (disk_map_ptr)malloc(sizeof(disk_map));
    if (unlikely(NULL == dmp))
    {
ErrorExit:
      free_disk_map(head);
      return NULL;
    }
    memset(dmp, 0, sizeof(disk_map)); // this is the Master Boot Record itself (all zeros/false)

    strcpy(dmp->description, is_mbr ? "Master Boot Record (MBR)" : "Extended Partition Table");
    dmp->start_lba = dmp->end_lba = mpsp->sp->lba;

    dmp->prev = tail;
    if (NULL == tail)
      head = dmp;
    else
      tail->next = dmp;
    tail = dmp;

    // work on the entries (4 for MBR, 2 for extended partition table)

    for (i = 0; i < ((uint32_t)(is_mbr ? 4 : 2)); i++)
    {
      if (0x00 != mpsp->part_table[i].part_type)
      {
        dmp = (disk_map_ptr)malloc(sizeof(disk_map));
        if (unlikely(NULL == dmp))
          goto ErrorExit;
        memset(dmp, 0, sizeof(disk_map));

        if (i != mpsp->ext_part_no)
        {
          dmp->start_lba = (uint64_t)mpsp->part_table[i].start_sector;
          dmp->end_lba = ((uint64_t)mpsp->part_table[i].start_sector) + ((uint64_t)mpsp->part_table[i].num_sectors) - 1;
          strcpy(dmp->description, mpsp->part_table[i].type_desc);

          dmp->prev = tail;
          if (NULL == tail)
            head = dmp;
          else
            tail->next = dmp;
          tail = dmp;
        }
        else
          free(dmp);
      }
    }

    mpsp = mpsp->next;
  }

  return head;
}

disk_map_ptr partition_create_disk_map_gpt(gpt_ptr g, gpt_ptr g2)
{
  disk_map_ptr          head = NULL, tail = NULL;
  disk_map_ptr          dmp;
  uint32_t              i, j;
  char                  str[40];

  // add the MBR = Master Boot Record itself

  dmp = (disk_map_ptr)malloc(sizeof(disk_map));
  if (unlikely(NULL == dmp))
  {
  ErrorExit:
    free_disk_map(head);
    return NULL;
  }
  memset(dmp, 0, sizeof(disk_map)); // this is the Master Boot Record itself (all zeros/false)

  strcpy(dmp->description, "Master Boot Record (MBR)");

  head = tail = dmp;

  // add the GPT header (primary GPT)

  dmp = (disk_map_ptr)malloc(sizeof(disk_map));
  if (unlikely(NULL == dmp))
    goto ErrorExit;
  memset(dmp, 0, sizeof(disk_map));
  strcpy(dmp->description, "GPT header (primary)");
  dmp->start_lba = g->header.current_lba;
  dmp->end_lba = dmp->start_lba;
  tail->next = dmp;
  dmp->prev = tail;
  tail = dmp;

  // add the GPT entries (primary)

  dmp = (disk_map_ptr)malloc(sizeof(disk_map));
  if (unlikely(NULL == dmp))
    goto ErrorExit;
  memset(dmp, 0, sizeof(disk_map));
  strcpy(dmp->description, "GPT entries (primary)");
  dmp->start_lba = g->header.starting_lba_part_entries;
  dmp->end_lba = dmp->start_lba + (g->header.number_of_part_entries >> 2) - 1;
  tail->next = dmp;
  dmp->prev = tail;
  tail = dmp;

  // add the GPT header (secondary GPT)

  dmp = (disk_map_ptr)malloc(sizeof(disk_map));
  if (unlikely(NULL == dmp))
    goto ErrorExit;
  memset(dmp, 0, sizeof(disk_map));
  strcpy(dmp->description, "GPT header (backup)");
  dmp->start_lba = g2->header.current_lba;
  dmp->end_lba = dmp->start_lba;
  tail->next = dmp;
  dmp->prev = tail;
  tail = dmp;

  // add the GPT entries (secondary)

  dmp = (disk_map_ptr)malloc(sizeof(disk_map));
  if (unlikely(NULL == dmp))
    goto ErrorExit;
  memset(dmp, 0, sizeof(disk_map));
  strcpy(dmp->description, "GPT entries (secondary)");
  dmp->start_lba = g2->header.starting_lba_part_entries;
  dmp->end_lba = dmp->start_lba + (g2->header.number_of_part_entries >> 2) - 1;
  tail->next = dmp;
  dmp->prev = tail;
  tail = dmp;

  // work on 128 entries in GPT

  for (i = 0; i < 128; i++)
  {
    if (is_zero_guid(g->entries[i].partition_guid) && is_zero_guid(g->entries[i].type_guid))
      continue;

    format_guid(str, g->entries[i].type_guid, false);

    dmp = (disk_map_ptr)malloc(sizeof(disk_map));
    if (unlikely(NULL == dmp))
      goto ErrorExit;
    memset(dmp, 0, sizeof(disk_map));

    dmp->start_lba = g->entries[i].part_start_lba;
    dmp->end_lba = g->entries[i].part_end_lba;

    for (j = 0; j < sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]); j++)
      if (!memcmp(part_type_table_gpt[j].guid_str, str, 36))
        break;

    if (j == (sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]))) // unknown
      strncpy(dmp->description, "GPT partition (unknown)", sizeof(dmp->description) - 1);
    else
      strncpy(dmp->description, part_type_table_gpt[j].gpt_description, sizeof(dmp->description) - 1);

    tail->next = dmp;
    dmp->prev = tail;
    tail = dmp;
  } // of for i

  return head;
}

uint32_t partition_peek_filesystem(disk_ptr dp, DISK_HANDLE h, uint64_t lba_start, uint8_t* uuid)
{
  sector_ptr              sp = disk_read_sectors(dp, h, NULL, NULL, lba_start, 3); // mostly LBA 0 within partition is sufficient but not for EXT2/3/4 where sector #2 has to be inspected (LBA 2 within partition)
  uint32_t                fs_type = FSYS_UNKNOWN;

  if (NULL == sp)
    return FSYS_UNKNOWN;

  if (!memcmp(&sp->data[0x36], "FAT12   ", 8))
  {
    fs_type = FSYS_WIN_FAT12;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x36], "FAT16   ", 8))
  {
    fs_type = FSYS_WIN_FAT16;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x52], "FAT16   ", 8))
  {
    fs_type = FSYS_WIN_FAT16;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x36], "FAT32   ", 8))
  {
    fs_type = FSYS_WIN_FAT32;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x52], "FAT32   ", 8))
  {
    fs_type = FSYS_WIN_FAT32;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x03], "EXFAT   ", 8))
  {
    fs_type = FSYS_WIN_EXFAT;
    goto do_exit;
  }
  if (!memcmp(&sp->data[0x03], "NTFS    ", 8))
  {
    fs_type = FSYS_WIN_NTFS;
    goto do_exit;
  }

  // check LBA 2 for specific Linux EXT filesystems (2, 3, and 4)

  if (0x53 != sp->data[0x438] || 0xEF != sp->data[0x439])
    goto do_exit;

  if (NULL != uuid)
    memcpy(uuid, &sp->data[0x468], 16); // this is full BIG ENDIAN UUID (not mixed endian as in GPT)

  // 32bit Little Endian at 0x5C means: just check that byte!

  if (0 == (0x04 & sp->data[0x45C])) // no journal
  {
    fs_type = FSYS_LINUX_EXT2;
    goto do_exit;
  }

  // EXT3 or EXT4

  if (READ_LITTLE_ENDIAN32(sp->data, 0x464) < 0x00000008)
  {
    fs_type = FSYS_LINUX_EXT3;
    goto do_exit;
  }

  fs_type = FSYS_LINUX_EXT4;

do_exit:

  disk_free_sector(sp);
  
  return fs_type;
}

bool partition_dump_mbr(disk_ptr dp)
{
  uint32_t            i;
  char                size_str[16];
  mbr_part_sector_ptr mpsp = dp->mbr;
  bool                is_mbr;
  char                description[64];

  if (NULL == mpsp)
    return false;

  if (dp->flags & DISK_FLAG_MBR_IS_PROTECTIVE)
    fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": MBR is a PROTECTIVE MBR.\n");

  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": MBR disk signature is 0x%08X\n\n", dp->mbr->disk_signature);

  fprintf(stdout, CTRL_BLUE "B  " CTRL_YELLOW "TY  " CTRL_GREEN "C/H/S start   C/H/S end    " CTRL_MAGENTA "start sec    sec num     " CTRL_RED "size     " CTRL_RESET "type string\n");
  fprintf(stdout, "----------------------------------------------------------------------------------------------------------------------\n");

  while (NULL != mpsp)
  {
    is_mbr = (0 == mpsp->sp->lba) ? true : false;

    if (is_mbr)
      fprintf(stdout, CTRL_MAGENTA "MASTER BOOT RECORD:" CTRL_RESET "\n");
    else
      fprintf(stdout, CTRL_MAGENTA "EXTENDED PARTITION TABLE:" CTRL_RESET "\n");

    for (i = 0; i < (uint32_t)(is_mbr ? 4 : 2); i++)
    {
      format_disk_size(((uint64_t)mpsp->part_table[i].num_sectors) * ((uint64_t)SECTOR_SIZE), size_str, sizeof(size_str));

      if (FSYS_UNKNOWN == mpsp->part_table[i].fs_type)
        strncpy(description, mpsp->part_table[i].type_desc, sizeof(description) - 1);
      else
        snprintf(description, sizeof(description), "%s [%s]", mpsp->part_table[i].type_desc, fsTypeStrings[mpsp->part_table[i].fs_type]);

      fprintf(stdout, CTRL_BLUE "%c  " CTRL_YELLOW "%02X  " CTRL_GREEN "%4u/%3u/%2u  %4u/%3u/%2u  " CTRL_MAGENTA "%10"FMT64"u  %10u  " CTRL_RED "%9s  " CTRL_RESET "%s\n", 0x80 == mpsp->part_table[i].boot_flag ? '*' : ' ',
        mpsp->part_table[i].part_type,
        mpsp->part_table[i].cylinder_first, mpsp->part_table[i].head_first, mpsp->part_table[i].sector_first,
        mpsp->part_table[i].cylinder_last, mpsp->part_table[i].head_last, mpsp->part_table[i].sector_last,
        mpsp->part_table[i].start_sector, mpsp->part_table[i].num_sectors, size_str,
        description);
    }

    mpsp = mpsp->next;
  }

  return true;
}

bool partition_dump_gpt(disk_ptr dp)
{
  uint32_t            i, j;
  char                size_str[16], str[40];
  uint64_t            attr;
  bool                first;
  char                fs_uuid_str[40];
  gpt_ptr             g;
  uint64_t            primary_lba, backup_lba;

  g = dp->primary_gpt_corrupt ? NULL : dp->gpt1;
  if (NULL == g)
  {
    g = dp->backup_gpt_corrupt ? NULL : dp->gpt2;
    if (NULL != g)
    {
      primary_lba = g->header.backup_lba;
      backup_lba = g->header.current_lba;
    }
  }
  else
  {
    primary_lba = g->header.current_lba;
    backup_lba = g->header.backup_lba;
  }

  if (NULL == g)
    return false;

  format_guid(str, g->header.disk_guid, false);
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": disk GUID is " CTRL_GREEN "%s" CTRL_RESET "\n\n", str);

  fprintf(stdout, "  " CTRL_CYAN "revision ........................: " CTRL_MAGENTA "0x%08X\n", g->header.revision);
  fprintf(stdout, "  " CTRL_CYAN "header size .....................: " CTRL_MAGENTA "0x%08X\n", g->header.header_size);
  fprintf(stdout, "  " CTRL_CYAN "header CRC32 ....................: " CTRL_MAGENTA "0x%08X\n", g->header.header_crc32);
  fprintf(stdout, "  " CTRL_CYAN "primary GPT at LBA ..............: " CTRL_MAGENTA "%"FMT64"u -> is corrupt? %s\n", primary_lba, dp->primary_gpt_corrupt ? CTRL_RED "yes" CTRL_RESET : CTRL_GREEN "no" CTRL_RESET);
  fprintf(stdout, "  " CTRL_CYAN "backup GPT at LBA .... ..........: " CTRL_MAGENTA "%"FMT64"u -> is corrupt? %s\n", backup_lba, dp->backup_gpt_corrupt ? CTRL_RED "yes" CTRL_RESET : CTRL_GREEN "no" CTRL_RESET);
  fprintf(stdout, "  " CTRL_CYAN "first usable LBA ................: " CTRL_MAGENTA "%"FMT64"u\n", g->header.first_usable_lba);
  fprintf(stdout, "  " CTRL_CYAN "last usable LBA .................: " CTRL_MAGENTA "%"FMT64"u\n", g->header.last_usable_lba);

  format_disk_size((g->header.last_usable_lba - g->header.first_usable_lba + 1) << 9, str, sizeof(str));

  fprintf(stdout, "  " CTRL_CYAN "  => number of usable sectors ...: " CTRL_MAGENTA "%"FMT64"u is approx. " CTRL_GREEN "%s" CTRL_RESET "\n", g->header.last_usable_lba - g->header.first_usable_lba + 1, str);
  fprintf(stdout, "  " CTRL_CYAN "part. entries CRC32 .............: " CTRL_MAGENTA "0x%08X" CTRL_RESET "\n\n", g->header.part_entries_crc32);

  for (i = 0; i < 128; i++)
  {
    if ((is_zero_guid(g->entries[i].partition_guid)) && (is_zero_guid(g->entries[i].type_guid)))
      continue;

    fprintf(stdout, "GPT partition entry %u of 128:\n", i + 1);

    format_guid(str, g->entries[i].partition_guid, false);
    fprintf(stdout, "  Partition GUID ..........: " CTRL_GREEN "%s" CTRL_RESET "\n", str);
    format_guid(str, g->entries[i].type_guid, false);
    fprintf(stdout, "  Type GUID ...............: " CTRL_YELLOW "%s" CTRL_RESET " => " CTRL_MAGENTA, str);

    for (j = 0; j < sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]); j++)
      if (!memcmp(part_type_table_gpt[j].guid_str, str, 36))
        break;

    if (j == (sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]))) // unknown
      fprintf(stdout, "*** UNKNOWN ***\n");
    else
      fprintf(stdout, "%s\n", part_type_table_gpt[j].gpt_description);

    format_disk_size((g->entries[i].part_end_lba - g->entries[i].part_start_lba + 1) << 9, size_str, sizeof(size_str));
    fprintf(stdout, CTRL_RESET "  Start and end LBA .......: " CTRL_GREEN "%"FMT64"u" CTRL_RESET " to " CTRL_GREEN "%"FMT64"u" CTRL_RESET " (size approx. " CTRL_MAGENTA "%s" CTRL_RESET ")\n", g->entries[i].part_start_lba, g->entries[i].part_end_lba, size_str);

    attr = g->entries[i].attributes;
    fprintf(stdout, "  Partition attributes ....: " CTRL_CYAN);
    if (0 == attr)
      fprintf(stdout, "NONE (0x0)" CTRL_RESET "\n");
    else
    {
      first = true;
      if (attr & GPT_ATTR_SYSTEM_PARTITION)
      {
        attr &= ~GPT_ATTR_SYSTEM_PARTITION;
        fprintf(stdout, "SYSTEM");
        first = false;
      }
      if (attr & GPT_ATTR_HIDE_EFI)
      {
        attr &= ~GPT_ATTR_HIDE_EFI;
        fprintf(stdout, first ? "HIDE" : "| HIDE");
        first = false;
      }
      if (attr & GPT_ATTR_LEGACY_BIOS_BOOT)
      {
        attr &= ~GPT_ATTR_LEGACY_BIOS_BOOT;
        fprintf(stdout, first ? "BOOT" : "| BOOT");
        first = false;
      }
      if (attr & GPT_ATTR_READ_ONLY)
      {
        attr &= ~GPT_ATTR_READ_ONLY;
        fprintf(stdout, first ? "R/O" : "| R/O");
        first = false;
      }
      if (attr & GPT_ATTR_HIDDEN)
      {
        attr &= ~GPT_ATTR_HIDDEN;
        fprintf(stdout, first ? "HIDDEN" : "| HIDDEN");
        first = false;
      }
      if (attr & GPT_ATTR_DO_NOT_MOUNT)
      {
        attr &= ~GPT_ATTR_DO_NOT_MOUNT;
        fprintf(stdout, first ? "NOMOUNT" : "| NOMOUNT");
        first = false;
      }
      if (0 != attr)
      {
        fprintf(stdout, first ? "additional unknown flags 0x%"FMT64"X" : "| additional unknown flags 0x%"FMT64"X", attr);
      }
      fprintf(stdout, CTRL_RESET "\n");
    }
    fprintf(stdout, "  Partition name ..........: '" CTRL_RED "%s" CTRL_RESET "'\n", g->entries[i].part_name_utf8_oem);

    format_guid(fs_uuid_str, g->entries[i].fs_uuid, true); // only meaningful for EXT2, EXT3, and EXT4

    switch (g->entries[i].fs_type)
    {
      case FSYS_WIN_FAT12:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT12" CTRL_RESET "'\n");
        break;
      case FSYS_WIN_FAT16:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT16" CTRL_RESET "'\n");
        break;
      case FSYS_WIN_FAT32:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT32" CTRL_RESET "'\n");
        break;
      case FSYS_WIN_EXFAT:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows exFAT" CTRL_RESET "'\n");
        break;
      case FSYS_WIN_NTFS:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows NTFS" CTRL_RESET "'\n");
        break;
      case FSYS_LINUX_EXT2:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT2" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
        break;
      case FSYS_LINUX_EXT3:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT3" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
        break;
      case FSYS_LINUX_EXT4:
        fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT4" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
        break;
      default:
        break;
    }
    fprintf(stdout, "\n");
  }

  return true;
}

bool partition_dump_temporary_gpt(gpt_ptr g)
{
  uint32_t            i, j;
  char                size_str[16], str[40];
  uint64_t            attr;
  bool                first;
  char                fs_uuid_str[40];

  format_guid(str, g->header.disk_guid, false);
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": disk GUID is " CTRL_GREEN "%s" CTRL_RESET "\n\n", str);

  fprintf(stdout, "  " CTRL_CYAN "revision ........................: " CTRL_MAGENTA "0x%08X\n", g->header.revision);
  fprintf(stdout, "  " CTRL_CYAN "header size .....................: " CTRL_MAGENTA "0x%08X\n", g->header.header_size);
  fprintf(stdout, "  " CTRL_CYAN "first usable LBA ................: " CTRL_MAGENTA "%"FMT64"u\n", g->header.first_usable_lba);
  fprintf(stdout, "  " CTRL_CYAN "last usable LBA .................: " CTRL_MAGENTA "%"FMT64"u\n", g->header.last_usable_lba);

  format_disk_size((g->header.last_usable_lba - g->header.first_usable_lba + 1) << 9, str, sizeof(str));

  fprintf(stdout, "  " CTRL_CYAN "  => number of usable sectors ...: " CTRL_MAGENTA "%"FMT64"u is approx. " CTRL_GREEN "%s" CTRL_RESET "\n", g->header.last_usable_lba - g->header.first_usable_lba + 1, str);

  fprintf(stdout, "\n");

  for (i = 0; i < 128; i++)
  {
    if ((is_zero_guid(g->entries[i].partition_guid)) && (is_zero_guid(g->entries[i].type_guid)))
      continue;

    fprintf(stdout, "GPT partition entry %u of 128:\n", i + 1);

    format_guid(str, g->entries[i].partition_guid, false);
    fprintf(stdout, "  Partition GUID ..........: " CTRL_GREEN "%s" CTRL_RESET "\n", str);
    format_guid(str, g->entries[i].type_guid, false);
    fprintf(stdout, "  Type GUID ...............: " CTRL_YELLOW "%s" CTRL_RESET " => " CTRL_MAGENTA, str);

    for (j = 0; j < sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]); j++)
      if (!memcmp(part_type_table_gpt[j].guid_str, str, 36))
        break;

    if (j == (sizeof(part_type_table_gpt) / sizeof(part_type_table_gpt[0]))) // unknown
      fprintf(stdout, "*** UNKNOWN ***\n");
    else
      fprintf(stdout, "%s\n", part_type_table_gpt[j].gpt_description);

    format_disk_size((g->entries[i].part_end_lba - g->entries[i].part_start_lba + 1) << 9, size_str, sizeof(size_str));
    fprintf(stdout, CTRL_RESET "  Start and end LBA .......: " CTRL_GREEN "%"FMT64"u" CTRL_RESET " to " CTRL_GREEN "%"FMT64"u" CTRL_RESET " (size approx. " CTRL_MAGENTA "%s" CTRL_RESET ")\n", g->entries[i].part_start_lba, g->entries[i].part_end_lba, size_str);

    attr = g->entries[i].attributes;
    fprintf(stdout, "  Partition attributes ....: " CTRL_CYAN);
    if (0 == attr)
      fprintf(stdout, "NONE (0x0)" CTRL_RESET "\n");
    else
    {
      first = true;
      if (attr & GPT_ATTR_SYSTEM_PARTITION)
      {
        attr &= ~GPT_ATTR_SYSTEM_PARTITION;
        fprintf(stdout, "SYSTEM");
        first = false;
      }
      if (attr & GPT_ATTR_HIDE_EFI)
      {
        attr &= ~GPT_ATTR_HIDE_EFI;
        fprintf(stdout, first ? "HIDE" : "| HIDE");
        first = false;
      }
      if (attr & GPT_ATTR_LEGACY_BIOS_BOOT)
      {
        attr &= ~GPT_ATTR_LEGACY_BIOS_BOOT;
        fprintf(stdout, first ? "BOOT" : "| BOOT");
        first = false;
      }
      if (attr & GPT_ATTR_READ_ONLY)
      {
        attr &= ~GPT_ATTR_READ_ONLY;
        fprintf(stdout, first ? "R/O" : "| R/O");
        first = false;
      }
      if (attr & GPT_ATTR_HIDDEN)
      {
        attr &= ~GPT_ATTR_HIDDEN;
        fprintf(stdout, first ? "HIDDEN" : "| HIDDEN");
        first = false;
      }
      if (attr & GPT_ATTR_DO_NOT_MOUNT)
      {
        attr &= ~GPT_ATTR_DO_NOT_MOUNT;
        fprintf(stdout, first ? "NOMOUNT" : "| NOMOUNT");
        first = false;
      }
      if (0 != attr)
      {
        fprintf(stdout, first ? "additional unknown flags 0x%"FMT64"X" : "| additional unknown flags 0x%"FMT64"X", attr);
      }
      fprintf(stdout, CTRL_RESET "\n");
    }
    fprintf(stdout, "  Partition name ..........: '" CTRL_RED "%s" CTRL_RESET "'\n", g->entries[i].part_name_utf8_oem);

    format_guid(fs_uuid_str, g->entries[i].fs_uuid, true); // only meaningful for EXT2, EXT3, and EXT4

    switch (g->entries[i].fs_type)
    {
    case FSYS_WIN_FAT12:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT12" CTRL_RESET "'\n");
      break;
    case FSYS_WIN_FAT16:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT16" CTRL_RESET "'\n");
      break;
    case FSYS_WIN_FAT32:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows FAT32" CTRL_RESET "'\n");
      break;
    case FSYS_WIN_EXFAT:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows exFAT" CTRL_RESET "'\n");
      break;
    case FSYS_WIN_NTFS:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Windows NTFS" CTRL_RESET "'\n");
      break;
    case FSYS_LINUX_EXT2:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT2" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
      break;
    case FSYS_LINUX_EXT3:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT3" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
      break;
    case FSYS_LINUX_EXT4:
      fprintf(stdout, "  File system in partition : '" CTRL_MAGENTA "Linux EXT4" CTRL_RESET "' (UUID %s)\n", fs_uuid_str);
      break;
    default:
      break;
    }
    fprintf(stdout, "\n");
  }

  return true;
}

void setGPTPartitionName(uint16_t* p, char *p2, const char* name)
{
  uint32_t i, l = (uint32_t)strlen(name);

  if (l > 35)
    l = 35;

  memcpy(p2, name, l);
  p2[l] = 0;

  for (i = 0; i < l; i++)
    *((uint8_t*)(p+i)) = name[i]; // UTF-16, Little Endian!
    
  p[l] = 0;
}

void create_protective_mbr(uint64_t device_sectors, uint8_t *target)
{
  uint32_t            num_sectors, write_sectors, start_cyl, start_head, start_sector, end_cyl, end_head, end_sector;
  uint64_t            start_lba, end_lba;
  
  // zero-out MBR
   
  memset(target, 0x00, SECTOR_SIZE);
  target[SECTOR_SIZE - 2] = 0x55;
  target[SECTOR_SIZE - 1] = 0xAA;

  // set sectors
  
  // partition is [1..device_sectors-1]; number is device_sectors-1-1+1 = device_sectors-1

  if (device_sectors >= 0x100000000)
    num_sectors = write_sectors = 0xFFFFFFFF;
  else
  {
    num_sectors = (uint32_t)device_sectors;
    write_sectors = num_sectors - 1;
  }

  start_lba = 1;
  end_lba = 1 + ((uint64_t)num_sectors - 1/* minus MBR */) - 1;

  lba2chs(start_lba, &start_cyl, &start_head, &start_sector);
  lba2chs(end_lba, &end_cyl, &end_head, &end_sector);

  target[0x01BE + 0x01] = (uint8_t)start_head;
  target[0x01BE + 0x02] = (uint8_t)(start_sector | ((start_cyl >> 8) << 6));
  target[0x01BE + 0x03] = (uint8_t)start_cyl;

  target[0x01BE + 0x04] = 0xEE; // type 0xEE is for the protective MBR, i.e. GPT follows this MBR

  target[0x01BE + 0x05] = (uint8_t)end_head;
  target[0x01BE + 0x06] = (uint8_t)(end_sector | ((end_cyl >> 8) << 6));
  target[0x01BE + 0x07] = (uint8_t)end_cyl;

  WRITE_LITTLE_ENDIAN32(target, 0x01BE + 0x08, 1); // start sector 1 is primary GPT header
  WRITE_LITTLE_ENDIAN32(target, 0x01BE + 0x0C, write_sectors);
}

extern const uint8_t guid_empty_partition[16];

void gpt_create_table(uint8_t* sector, gpt_ptr g, bool is_primary)
{
  uint32_t          i, header_ofs, orig_entry_ofs, entry_ofs, crc32;

  if (is_primary)
  {
    header_ofs = 0;
    orig_entry_ofs = entry_ofs = SECTOR_SIZE;
  }
  else
  {
    header_ofs = 32 * SECTOR_SIZE;
    orig_entry_ofs = entry_ofs = 0;
  }

  memset(sector, 0x00, 33 * SECTOR_SIZE); // header plus entries (primary) or entries plus header (secondary)

  // create header

  memcpy(&sector[header_ofs + 0x0000], "EFI PART", 8);
  sector[header_ofs + 0x000A] = 0x01; // 0x0008, length 4, revision 00, 00, 01, 00
  sector[header_ofs + 0x000C] = 0x5C; // 0x000C, length 4, header size 0x5C

  // 0x0010: CRC32

  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0018, g->header.current_lba);
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0020, g->header.backup_lba);
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0028, g->header.first_usable_lba);
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0030, g->header.last_usable_lba);
  memcpy(&sector[header_ofs + 0x0038], g->header.disk_guid, 16);
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0048, g->header.starting_lba_part_entries);
  sector[header_ofs + 0x0050] = 0x80; // length 4, number of entries = 128
  sector[header_ofs + 0x0054] = 0x80; // length 4, size of one entry = 128

  // 0x0058: CRC32 of entries

  // add all entries

  for (i = 0; i < 128; i++)
  {
    if (memcmp(guid_empty_partition, g->entries[i].type_guid, 16))
    {
      memcpy(&sector[entry_ofs + 0x0000], g->entries[i].type_guid, 16);
      memcpy(&sector[entry_ofs + 0x0010], g->entries[i].partition_guid, 16);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0020, g->entries[i].part_start_lba);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0028, g->entries[i].part_end_lba);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0030, g->entries[i].attributes);
      memcpy(&sector[entry_ofs + 0x0038], g->entries[i].part_name, 36 * sizeof(uint16_t));
    }

    entry_ofs += 0x0080;
  }

  // compute CRC32 of header (please note that CRC32 itself is currently zero) and entries

  crc32 = calc_crc32(&sector[orig_entry_ofs], 128 * 128, 0xFFFFFFFF);
  WRITE_LITTLE_ENDIAN32(sector, header_ofs + 0x0058, crc32);

  crc32 = calc_crc32(&sector[header_ofs], 0x5C, 0xFFFFFFFF);
  WRITE_LITTLE_ENDIAN32(sector, header_ofs + 0x0010, crc32);
}

uint64_t gpt_repair_table(uint8_t* sector, gpt_ptr g, bool is_primary )
{
  uint32_t          i, header_ofs, orig_entry_ofs, entry_ofs, crc32;

  if (is_primary) // if/else part exchanged from function gpt_create_table because either one repairs the other one
  {
    header_ofs = 32 * SECTOR_SIZE;
    orig_entry_ofs = entry_ofs = 0;
  }
  else
  {
    header_ofs = 0;
    orig_entry_ofs = entry_ofs = SECTOR_SIZE;
  }

  memset(sector, 0x00, 33 * SECTOR_SIZE); // header plus entries (primary) or entries plus header (secondary)

  // create header

  memcpy(&sector[header_ofs + 0x0000], "EFI PART", 8);
  sector[header_ofs + 0x000A] = 0x01; // 0x0008, length 4, revision 00, 00, 01, 00
  sector[header_ofs + 0x000C] = 0x5C; // 0x000C, length 4, header size 0x5C

  // 0x0010: CRC32

  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0018, g->header.backup_lba);   // exchanged primary and backup because either one repairs the other one
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0020, g->header.current_lba);  // exchanged primary and backup because either one repairs the other one
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0028, g->header.first_usable_lba);
  WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0030, g->header.last_usable_lba);
  memcpy(&sector[header_ofs + 0x0038], g->header.disk_guid, 16);

  // write location of GPT partition entries

  if (is_primary) // use primary to re-create backup GPT
    WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0048, g->header.backup_lba - 32);
  else
    WRITE_LITTLE_ENDIAN64(sector, header_ofs + 0x0048, ((uint64_t)2)); // primary GPT partition entries always start at 2
  
  sector[header_ofs + 0x0050] = 0x80; // length 4, number of entries = 128
  sector[header_ofs + 0x0054] = 0x80; // length 4, size of one entry = 128

  // 0x0058: CRC32 of entries

  // add all entries

  for (i = 0; i < 128; i++)
  {
    if (memcmp(guid_empty_partition, g->entries[i].type_guid, 16))
    {
      memcpy(&sector[entry_ofs + 0x0000], g->entries[i].type_guid, 16);
      memcpy(&sector[entry_ofs + 0x0010], g->entries[i].partition_guid, 16);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0020, g->entries[i].part_start_lba);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0028, g->entries[i].part_end_lba);
      WRITE_LITTLE_ENDIAN64(sector, entry_ofs + 0x0030, g->entries[i].attributes);
      memcpy(&sector[entry_ofs + 0x0038], g->entries[i].part_name, 36 * sizeof(uint16_t));
    }

    entry_ofs += 0x0080;
  }

  // compute CRC32 of header (please note that CRC32 itself is currently zero) and entries

  crc32 = calc_crc32(&sector[orig_entry_ofs], 128 * 128, 0xFFFFFFFF);
  WRITE_LITTLE_ENDIAN32(sector, header_ofs + 0x0058, crc32);

  crc32 = calc_crc32(&sector[header_ofs], 0x5C, 0xFFFFFFFF);
  WRITE_LITTLE_ENDIAN32(sector, header_ofs + 0x0010, crc32);

  return g->header.backup_lba << SECTOR_SHIFT;
}

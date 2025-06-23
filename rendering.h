// I couldn't bother to download, and parse the offsets. So here is a header file with the offsets defined.
// Make sure to update these offsets if they change.
#ifndef RENDERING_H
#define RENDERING_H

// Offsets.
#define ENTITYLIST_OFFSET      0x1A020A8        
#define VIEWMATRIX_OFFSET      0x1A6B230       
#define LOCALPLAYERPAWN_OFFSET 0x18560D0        
#define HPLAYERPAWN_OFFSET    0x824        
#define IHEALTH_OFFSET        0x344       
#define ITEAMNUM_OFFSET       0x3E3        
#define VOLDORIGIN_OFFSET     0x1324       

void dump_offsets(uintptr_t module_base);
#endif 

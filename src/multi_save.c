#include "global.h"
#include "PR/os_internal_flash.h"
#include "recompconfig.h"
#ifndef MM_MULTI_SAVE_ENGLISH
#define MM_MULTI_SAVE_ENGLISH 0
#endif
#include "z64rumble.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"

#include "generated/ui_glyphs.inc"
#include "generated/ui_textures.inc"

#define MS_SLOT_COUNT 6
#define MS_BACKEND_COUNT 3
#define MS_MAIN_COPY 6
#define MS_MAIN_ERASE 7
#define MS_MAIN_QUIT 8
#define MS_MAIN_BUTTON_COUNT 9

typedef enum {
    MS_MODE_MAIN,
    MS_MODE_COPY_SOURCE,
    MS_MODE_COPY_DEST,
    MS_MODE_COPY_CONFIRM,
    MS_MODE_COPY_DONE,
    MS_MODE_ERASE_SELECT,
    MS_MODE_ERASE_CONFIRM,
    MS_MODE_ERASE_DONE,
} MsMode;

typedef struct {
    u8 occupied;
    u8 owl;
    u8 name[8];
} MsSlotSummary;

extern u8 gFileSelFileNameBoxTex[];
extern u8 gFileSelConnectorTex[];
extern u8 gFileSelFile1ButtonENGTex[];
extern u8 gFileSelFile2ButtonENGTex[];
extern u8 gFileSelENDButtonENGTex[];
extern u8 gFileSelBlankButtonTex[];
extern u8 gFileSelOwlSaveIconTex[];
extern u8 gFileSelBigButtonHighlightTex[];
extern u8 gFileSelMediumButtonHighlightTex[];

extern s32 SysFlashrom_ReadData(void* addr, u32 pageNum, u32 pageCount);
extern void SysFlashrom_WriteDataSync(void* addr, u32 pageNum, u32 pageCount);

// Keep the Chinese and English packages on the same user-selected save group.
static const char* sMsBackendNames[MS_BACKEND_COUNT] = {
    "../Ultimate 6 Save Slots/slot1&2",
    "../Ultimate 6 Save Slots/slot3&4",
    "../Ultimate 6 Save Slots/slot5&6",
};
static MsSlotSummary sMsSlots[MS_SLOT_COUNT];
static FileSelectState* sMsFileSelect;
static MsMode sMsMode;
static s16 sMsCursor;
static s16 sMsSource;
static s16 sMsDest;
static s16 sMsConfirm;
static s16 sMsDoneTimer;
static s16 sMsActiveLogicalSlot;
static s16 sMsCurrentBackend;
static const u8 sMsEnglish = MM_MULTI_SAVE_ENGLISH ? 1 : 0;
static u8 sMsDrawFullUi;
static u8 sMsDrawMappedLabels;

static const u16 sMsCnCopy[] = { 0x590D, 0x5236 };
static const u16 sMsCnErase[] = { 0x5220, 0x9664 };
static const u16 sMsCnQuit[] = { 0x9000, 0x51FA };
static const u16 sMsCnConfirm[] = { 0x786E, 0x5B9A };

static s32 ms_slot_occupied(FileSelectState* fileSelect, s32 physicalSlot) {
    static const u8 marker[6] = { 'Z', 'E', 'L', 'D', 'A', '3' };
    s32 i;

    for (i = 0; i < 6; i++) {
        if (fileSelect->newf[physicalSlot][i] != marker[i]) {
            return false;
        }
    }
    return true;
}

static void ms_change_backend(s32 backend) {
    if (backend == sMsCurrentBackend) {
        return;
    }
    recomp_change_save_file(sMsBackendNames[backend]);
    sMsCurrentBackend = backend;
}

static void ms_flush_save_writes(void) {
    recomp_change_save_file(sMsBackendNames[sMsCurrentBackend]);
}

static void ms_capture_backend(FileSelectState* fileSelect, s32 backend) {
    s32 physical;
    s32 i;

    ms_change_backend(backend);
    func_801457CC(&fileSelect->state, &fileSelect->sramCtx);
    for (physical = 0; physical < 2; physical++) {
        MsSlotSummary* slot = &sMsSlots[(backend * 2) + physical];
        slot->occupied = ms_slot_occupied(fileSelect, physical);
        slot->owl = fileSelect->isOwlSave[physical + 2];
        for (i = 0; i < 8; i++) {
            slot->name[i] = fileSelect->fileNames[physical][i];
        }
    }
}

static void ms_scan_all(FileSelectState* fileSelect) {
    s32 backend;

    for (backend = 0; backend < MS_BACKEND_COUNT; backend++) {
        ms_capture_backend(fileSelect, backend);
    }
    ms_change_backend(0);
    func_801457CC(&fileSelect->state, &fileSelect->sramCtx);
}

static void ms_refresh_backend(FileSelectState* fileSelect, s32 backend) {
    ms_capture_backend(fileSelect, backend);
    if (backend != 0) {
        ms_capture_backend(fileSelect, 0);
    }
}

static void ms_load_logical_slot(FileSelectState* fileSelect, s32 logicalSlot) {
    s32 backend = logicalSlot / 2;
    s32 physical;
    s32 i;

    ms_change_backend(backend);
    func_801457CC(&fileSelect->state, &fileSelect->sramCtx);
    for (physical = 0; physical < 2; physical++) {
        if (!ms_slot_occupied(fileSelect, physical)) {
            for (i = 0; i < 8; i++) {
                fileSelect->fileNames[physical][i] = 0x3E;
            }
        }
    }
    sMsActiveLogicalSlot = logicalSlot;
}

static void ms_play_cursor(void) {
    Audio_PlaySfx(NA_SE_SY_FSEL_CURSOR);
}

static void ms_play_error(void) {
    Audio_PlaySfx(NA_SE_SY_FSEL_ERROR);
}

static void ms_begin_name_entry(FileSelectState* fileSelect, s32 physicalSlot) {
    s32 i;

    Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
    fileSelect->buttonIndex = physicalSlot;
    fileSelect->configMode = CM_ROTATE_TO_NAME_ENTRY;
    fileSelect->kbdButton = FS_KBD_BTN_NONE;
    fileSelect->charPage = FS_CHAR_PAGE_ENG;
    fileSelect->kbdX = 0;
    fileSelect->kbdY = 0;
    fileSelect->charIndex = 0;
    fileSelect->charBgAlpha = 0;
    fileSelect->newFileNameCharCount = 0;
    fileSelect->nameEntryBoxPosX = 120;
    fileSelect->nameEntryBoxAlpha = 0;
    for (i = 0; i < 8; i++) {
        fileSelect->fileNames[physicalSlot][i] = 0x3E;
    }
}

static void ms_open_slot(FileSelectState* fileSelect, s32 logicalSlot) {
    s32 physicalSlot = logicalSlot & 1;

    ms_load_logical_slot(fileSelect, logicalSlot);
    fileSelect->buttonIndex = physicalSlot;
    if (!sMsSlots[logicalSlot].occupied) {
        ms_begin_name_entry(fileSelect, physicalSlot);
        return;
    }

    Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
    fileSelect->actionTimer = 4;
    fileSelect->selectMode = SM_FADE_MAIN_TO_SELECT;
    fileSelect->selectedFileIndex = physicalSlot;
    fileSelect->menuMode = FS_MENU_MODE_SELECT;
    fileSelect->nextTitleLabel = FS_TITLE_OPEN_FILE;
}

static s32 ms_count_occupied(void) {
    s32 i;
    s32 count = 0;

    for (i = 0; i < MS_SLOT_COUNT; i++) {
        count += sMsSlots[i].occupied != 0;
    }
    return count;
}

static void ms_enter_mode(FileSelectState* fileSelect, MsMode mode, s16 cursor, s16 title) {
    sMsMode = mode;
    sMsCursor = cursor;
    fileSelect->buttonIndex = 0;
    fileSelect->configMode = CM_UNUSED_31;
    fileSelect->titleLabel = title;
    fileSelect->nextTitleLabel = title;
}

static s32 ms_copy_region(FileSelectState* fileSelect, s32 sourceBackend, u32 sourcePage,
                          s32 destBackend, u32 destPage, u32 pageCount) {
    if ((destPage % FLASH_BLOCK_SIZE) != 0) {
        return false;
    }
    ms_change_backend(sourceBackend);
    if (SysFlashrom_ReadData(fileSelect->sramCtx.saveBuf, sourcePage, pageCount) != 0) {
        return false;
    }
    ms_change_backend(destBackend);
    SysFlashrom_WriteDataSync(fileSelect->sramCtx.saveBuf, destPage, pageCount);
    ms_flush_save_writes();
    return true;
}

static s32 ms_copy_slot(FileSelectState* fileSelect, s32 source, s32 dest) {
    s32 sourceBackend = source / 2;
    s32 destBackend = dest / 2;
    s32 sourcePhysical = source & 1;
    s32 destPhysical = dest & 1;
    s32 sourceIndex = sourcePhysical * 2;
    s32 destIndex = destPhysical * 2;

    if (!ms_copy_region(fileSelect, sourceBackend, gFlashSaveStartPages[sourceIndex],
                        destBackend, gFlashSaveStartPages[destIndex],
                        gFlashSaveNumPages[sourceIndex] + gFlashSaveNumPages[sourceIndex + 1])) {
        return false;
    }
    if (!ms_copy_region(fileSelect, sourceBackend, gFlashOwlSaveStartPages[sourceIndex + 1],
                        destBackend, gFlashOwlSaveStartPages[destIndex + 1],
                        gFlashOwlSaveNumPages[sourceIndex + 1])) {
        return false;
    }
    if (!ms_copy_region(fileSelect, sourceBackend, gFlashOwlSaveStartPages[sourceIndex],
                        destBackend, gFlashOwlSaveStartPages[destIndex],
                        gFlashOwlSaveNumPages[sourceIndex])) {
        return false;
    }
    ms_refresh_backend(fileSelect, destBackend);
    return true;
}

static void ms_erase_slot(FileSelectState* fileSelect, s32 logicalSlot) {
    s32 backend = logicalSlot / 2;
    s32 physical = logicalSlot & 1;
    s32 index = physical * 2;

    bzero(fileSelect->sramCtx.saveBuf, SAVE_BUFFER_SIZE);
    ms_change_backend(backend);
    SysFlashrom_WriteDataSync(fileSelect->sramCtx.saveBuf, gFlashSaveStartPages[index],
                              gFlashSaveNumPages[index] + gFlashSaveNumPages[index + 1]);
    ms_flush_save_writes();
    SysFlashrom_WriteDataSync(fileSelect->sramCtx.saveBuf, gFlashOwlSaveStartPages[index + 1],
                              gFlashOwlSaveNumPages[index + 1]);
    ms_flush_save_writes();
    SysFlashrom_WriteDataSync(fileSelect->sramCtx.saveBuf, gFlashOwlSaveStartPages[index],
                              gFlashOwlSaveNumPages[index]);
    ms_flush_save_writes();
    ms_refresh_backend(fileSelect, backend);
}

static void ms_return_main(FileSelectState* fileSelect, s16 cursor) {
    sMsMode = MS_MODE_MAIN;
    sMsCursor = cursor;
    sMsConfirm = 0;
    fileSelect->buttonIndex = 0;
    fileSelect->menuMode = FS_MENU_MODE_CONFIG;
    fileSelect->configMode = CM_MAIN_MENU;
    fileSelect->titleLabel = FS_TITLE_SELECT_FILE;
    fileSelect->nextTitleLabel = FS_TITLE_SELECT_FILE;
}

static void ms_move_slot_cursor(FileSelectState* fileSelect) {
    if (ABS_ALT(fileSelect->stickAdjY) > 30) {
        ms_play_cursor();
        if (fileSelect->stickAdjY > 30) {
            sMsCursor = (sMsCursor + MS_SLOT_COUNT - 1) % MS_SLOT_COUNT;
        } else {
            sMsCursor = (sMsCursor + 1) % MS_SLOT_COUNT;
        }
    }
}

static void ms_update_confirm(FileSelectState* fileSelect) {
    if ((ABS_ALT(fileSelect->stickAdjX) > 30) || (ABS_ALT(fileSelect->stickAdjY) > 30)) {
        ms_play_cursor();
        sMsConfirm ^= 1;
    }
}

static const u8* ms_find_glyph(u16 codepoint) {
    s32 low = 0;
    s32 high = MS_UI_GLYPH_COUNT - 1;

    while (low <= high) {
        s32 mid = low + ((high - low) / 2);
        if (sMsUiGlyphCodepoints[mid] == codepoint) {
            return &sMsUiGlyphBitmaps[mid * MS_UI_GLYPH_BYTES];
        }
        if (sMsUiGlyphCodepoints[mid] < codepoint) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return NULL;
}

static void ms_set_quad(Vtx* vtx, s16 left, s16 top, s16 right, s16 bottom,
                        s16 textureWidth, s16 textureHeight) {
    s32 i;

    vtx[0].v.ob[0] = vtx[2].v.ob[0] = left;
    vtx[1].v.ob[0] = vtx[3].v.ob[0] = right;
    vtx[0].v.ob[1] = vtx[1].v.ob[1] = top;
    vtx[2].v.ob[1] = vtx[3].v.ob[1] = bottom;
    for (i = 0; i < 4; i++) {
        vtx[i].v.ob[2] = 0;
        vtx[i].v.flag = 0;
        vtx[i].v.cn[0] = 255;
        vtx[i].v.cn[1] = 255;
        vtx[i].v.cn[2] = 255;
        vtx[i].v.cn[3] = 255;
    }
    vtx[0].v.tc[0] = vtx[2].v.tc[0] = 0;
    vtx[1].v.tc[0] = vtx[3].v.tc[0] = textureWidth << 5;
    vtx[0].v.tc[1] = vtx[1].v.tc[1] = 0;
    vtx[2].v.tc[1] = vtx[3].v.tc[1] = textureHeight << 5;
}

static void ms_draw_ia16(GraphicsContext* gfxCtx, Gfx** gfxP, const u16* texture,
                         s16 width, s16 height, s16 left, s16 top, s16 alpha,
                         s16 red, s16 green, s16 blue) {
    Gfx* gfx = *gfxP;
    Vtx* vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(Vtx));

    if (vtx == NULL) {
        return;
    }
    ms_set_quad(vtx, left, top, left + width, top - height, width, height);
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                      PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(gfx++, 0, 0, red, green, blue, alpha);
    gDPSetEnvColor(gfx++, 0, 0, 0, 0);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_IA, G_IM_SIZ_16b, width, height, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
    *gfxP = gfx;
}

static void ms_draw_ia8(GraphicsContext* gfxCtx, Gfx** gfxP, const u8* texture,
                        s16 width, s16 height, s16 left, s16 top, s16 alpha,
                        s16 red, s16 green, s16 blue) {
    Gfx* gfx = *gfxP;
    Vtx* vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(Vtx));

    if (vtx == NULL) {
        return;
    }
    ms_set_quad(vtx, left, top, left + width, top - height, width, height);
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, red, green, blue, alpha);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_IA, G_IM_SIZ_8b, width, height, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
    *gfxP = gfx;
}

static void ms_draw_i8(GraphicsContext* gfxCtx, Gfx** gfxP, const u8* texture,
                       s16 width, s16 height, s16 left, s16 top, s16 alpha,
                       s16 red, s16 green, s16 blue) {
    Gfx* gfx = *gfxP;
    Vtx* vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(Vtx));

    if (vtx == NULL) {
        return;
    }
    ms_set_quad(vtx, left, top, left + width, top - height, width, height);
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0,
                      1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(gfx++, 0, 0, red, green, blue, alpha);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_I, G_IM_SIZ_8b, width, height, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
    *gfxP = gfx;
}

static void ms_draw_rgba32(GraphicsContext* gfxCtx, Gfx** gfxP, const u32* texture,
                           s16 width, s16 height, s16 left, s16 top, s16 alpha) {
    Gfx* gfx = *gfxP;
    Vtx* vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(Vtx));

    if (vtx == NULL) {
        return;
    }
    ms_set_quad(vtx, left, top, left + width, top - height, width, height);
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);
    gDPLoadTextureBlock(gfx++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, width, height, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
    *gfxP = gfx;
}

static void ms_draw_glyph(GraphicsContext* gfxCtx, Gfx** gfxP, u16 codepoint,
                          s16 left, s16 top, s16 size, s16 alpha) {
    const u8* texture = ms_find_glyph(codepoint);
    Gfx* gfx = *gfxP;
    Vtx* vtx;

    if (texture == NULL) {
        return;
    }
    vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(Vtx));
    if (vtx == NULL) {
        return;
    }
    ms_set_quad(vtx, left, top, left + size, top - size, MS_UI_GLYPH_SIZE, MS_UI_GLYPH_SIZE);
    gDPPipeSync(gfx++);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
                      0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    gDPSetPrimColor(gfx++, 0, 0, 255, 245, 217, alpha);
    gDPLoadTextureBlock_4b(gfx++, texture, G_IM_FMT_I, MS_UI_GLYPH_SIZE, MS_UI_GLYPH_SIZE, 0,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
    *gfxP = gfx;
}

static u8 ms_original_ascii_index(char character) {
    if ((character >= 'A') && (character <= 'Z')) {
        return 0x0A + (character - 'A');
    }
    if ((character >= 'a') && (character <= 'z')) {
        return 0x24 + (character - 'a');
    }
    if ((character >= '1') && (character <= '9')) {
        return character - '0';
    }
    if (character == '0') {
        return 0;
    }
    if (character == '.') {
        return 0x40;
    }
    if (character == '-') {
        return 0x3F;
    }
    return 0x3E;
}

static void ms_draw_original_ascii_centered(FileSelectState* fileSelect, Gfx** gfxP,
                                            const char* text, s16 centerX, s16 top,
                                            s16 size, s16 advance, s16 alpha) {
    Gfx* gfx = *gfxP;
    s16 length = 0;
    s16 i;
    s16 x;
    s32 pass;

    while (text[length] != '\0') {
        length++;
    }
    x = centerX - ((length * advance) / 2);
    for (pass = 0; pass < 2; pass++) {
        s16 color = pass ? 255 : 0;
        s16 offset = pass ? 0 : 1;

        gDPPipeSync(gfx++);
        gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
                          0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(gfx++, 0, 0, color, color, color, alpha);
        for (i = 0; i < length; i++) {
            Vtx* vtx = GRAPH_ALLOC(fileSelect->state.gfxCtx, 4 * sizeof(Vtx));
            const u8* texture = fileSelect->font.fontBuf +
                                (ms_original_ascii_index(text[i]) * FONT_CHAR_TEX_SIZE);

            if (vtx == NULL) {
                break;
            }
            ms_set_quad(vtx, x + (i * advance) + offset, top - offset,
                        x + (i * advance) + size + offset, top - size - offset, 16, 16);
            gDPLoadTextureBlock_4b(gfx++, texture, G_IM_FMT_I, 16, 16, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPVertex(gfx++, vtx, 4, 0);
            gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
        }
    }
    *gfxP = gfx;
}

static void ms_draw_u16_centered(GraphicsContext* gfxCtx, Gfx** gfxP, const u16* text,
                                 s16 length, s16 centerX, s16 top, s16 size,
                                 s16 advance, s16 alpha) {
    s16 i;
    s16 x = centerX - ((length * advance) / 2);

    for (i = 0; i < length; i++, x += advance) {
        ms_draw_glyph(gfxCtx, gfxP, text[i], x, top, size, alpha);
    }
}

static void ms_draw_font_name(FileSelectState* fileSelect, Gfx** gfxP, const u8* name,
                              s16 left, s16 top, s16 alpha) {
    Gfx* gfx = *gfxP;
    s32 pass;
    s32 i;

    for (pass = 0; pass < 2; pass++) {
        s16 color = pass ? 255 : 0;
        s16 offset = pass ? 0 : 1;
        gDPPipeSync(gfx++);
        gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
                          0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(gfx++, 0, 0, color, color, color, alpha);
        for (i = 0; i < 8; i++) {
            Vtx* vtx;
            const u8* texture;
            if (name[i] == 0x3E) {
                continue;
            }
            vtx = GRAPH_ALLOC(fileSelect->state.gfxCtx, 4 * sizeof(Vtx));
            if (vtx == NULL) {
                break;
            }
            texture = fileSelect->font.fontBuf + (name[i] * FONT_CHAR_TEX_SIZE);
            ms_set_quad(vtx, left + (i * 10) + D_80814280[name[i]] + offset, top - offset,
                        left + (i * 10) + D_80814280[name[i]] + 11 + offset,
                        top - 12 - offset, 16, 16);
            gDPLoadTextureBlock_4b(gfx++, texture, G_IM_FMT_I, 16, 16, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPVertex(gfx++, vtx, 4, 0);
            gSP1Quadrangle(gfx++, 0, 2, 3, 1, 0);
        }
    }
    *gfxP = gfx;
}

static void ms_draw_slot_label(FileSelectState* fileSelect, Gfx** gfxP, s32 logicalSlot,
                               s16 left, s16 top, s16 alpha) {
    if (sMsEnglish) {
        char label[] = { 'F', 'i', 'l', 'e', ' ', '1', '\0' };
        label[5] = '1' + logicalSlot;
        ms_draw_original_ascii_centered(fileSelect, gfxP, label, left + 30, top - 4, 8, 6, alpha);
    } else {
        u16 label[] = { 0x4FDD, 0x5B58, 0x6570, 0x636E, '1' };
        label[4] = '1' + logicalSlot;
        ms_draw_u16_centered(fileSelect->state.gfxCtx, gfxP, label, 5, left + 29, top - 3, 10, 9, alpha);
    }
}

static void ms_draw_slot_button(FileSelectState* fileSelect, Gfx** gfxP, s32 logicalSlot,
                                s16 left, s16 top, s16 alpha) {
    const u16* buttonTexture = (const u16*)((logicalSlot & 1) ? gFileSelFile2ButtonENGTex
                                                              : gFileSelFile1ButtonENGTex);

    ms_draw_ia16(fileSelect->state.gfxCtx, gfxP, buttonTexture, 64, 16,
                 left, top, alpha, fileSelect->windowColor[0],
                 fileSelect->windowColor[1], fileSelect->windowColor[2]);
    ms_draw_slot_label(fileSelect, gfxP, logicalSlot, left, top, alpha);
}

static void ms_draw_slot_row(FileSelectState* fileSelect, Gfx** gfxP, s32 logicalSlot,
                             s16 left, s16 top, s16 alpha) {
    MsSlotSummary* slot = &sMsSlots[logicalSlot];

    ms_draw_slot_button(fileSelect, gfxP, logicalSlot, left, top, alpha);
    if (slot->occupied) {
        ms_draw_ia16(fileSelect->state.gfxCtx, gfxP, (const u16*)gFileSelFileNameBoxTex,
                     108, 16, left + 64, top, alpha, fileSelect->windowColor[0],
                     fileSelect->windowColor[1], fileSelect->windowColor[2]);
        ms_draw_ia8(fileSelect->state.gfxCtx, gfxP, gFileSelConnectorTex, 24, 16,
                    left + 52, top, 255, fileSelect->windowColor[0],
                    fileSelect->windowColor[1], fileSelect->windowColor[2]);
        ms_draw_font_name(fileSelect, gfxP, slot->name, left + 78, top - 2, alpha);
        if (slot->owl) {
            ms_draw_ia16(fileSelect->state.gfxCtx, gfxP, (const u16*)gFileSelBlankButtonTex,
                          52, 16, left + 169, top, alpha, fileSelect->windowColor[0],
                         fileSelect->windowColor[1], fileSelect->windowColor[2]);
            ms_draw_rgba32(fileSelect->state.gfxCtx, gfxP, (const u32*)gFileSelOwlSaveIconTex,
                            24, 12, left + 183, top - 2, alpha);
        }
    }
}

static void ms_draw_action_button(FileSelectState* fileSelect, Gfx** gfxP, s16 left,
                                  s16 top, const u16* cnLabel, s16 cnLength,
                                  const u16* enTexture, s16 alpha) {
    const u16* buttonTexture = sMsEnglish ? enTexture
                                          : (const u16*)gFileSelENDButtonENGTex;
    s16 width = sMsEnglish ? 64 : 44;

    ms_draw_ia16(fileSelect->state.gfxCtx, gfxP, buttonTexture, width, 16,
                 left, top, alpha, fileSelect->windowColor[0],
                 fileSelect->windowColor[1], fileSelect->windowColor[2]);
    if (!sMsEnglish) {
        ms_draw_u16_centered(fileSelect->state.gfxCtx, gfxP, cnLabel, cnLength,
                             left + 22, top - 3, 10, 9, alpha);
    }
}

static s16 ms_action_button_left(s16 left, s16 index) {
    return sMsEnglish ? left + 6 + (index * 72) : left + 6 + (index * 52);
}

static s16 ms_confirm_button_left(s16 left, s16 index) {
    return sMsEnglish ? left + 42 + (index * 72) : left + 36 + (index * 56);
}

static void ms_draw_full_ui(FileSelectState* fileSelect) {
    Gfx* gfx;
    s16 left = fileSelect->windowPosX - 6;
    s16 row;
    s16 buttonTop = -50;
    s16 slotHighlightTop = 52 - (sMsCursor * 16);
    s16 uiAlpha = fileSelect->windowAlpha;

    OPEN_DISPS(fileSelect->state.gfxCtx);
    gfx = POLY_OPA_DISP;
    for (row = 0; row < MS_SLOT_COUNT; row++) {
        ms_draw_slot_row(fileSelect, &gfx, row, left, 48 - (row * 16), uiAlpha);
    }

    if (sMsMode == MS_MODE_MAIN) {
        ms_draw_action_button(fileSelect, &gfx, ms_action_button_left(left, 0), buttonTop,
                              sMsCnCopy, ARRAY_COUNT(sMsCnCopy), sMsCopyButtonTexture, uiAlpha);
        ms_draw_action_button(fileSelect, &gfx, ms_action_button_left(left, 1), buttonTop,
                              sMsCnErase, ARRAY_COUNT(sMsCnErase), sMsEraseButtonTexture, uiAlpha);
        ms_draw_action_button(fileSelect, &gfx, ms_action_button_left(left, 2), buttonTop,
                              sMsCnQuit, ARRAY_COUNT(sMsCnQuit), sMsQuitButtonTexture, uiAlpha);
    } else if ((sMsMode == MS_MODE_COPY_CONFIRM) || (sMsMode == MS_MODE_ERASE_CONFIRM)) {
        ms_draw_action_button(fileSelect, &gfx, ms_confirm_button_left(left, 0), buttonTop,
                              sMsCnConfirm, ARRAY_COUNT(sMsCnConfirm), sMsYesButtonTexture, uiAlpha);
        ms_draw_action_button(fileSelect, &gfx, ms_confirm_button_left(left, 1), buttonTop,
                              sMsCnQuit, ARRAY_COUNT(sMsCnQuit), sMsQuitButtonTexture, uiAlpha);
    }

    if ((fileSelect->configMode == CM_MAIN_MENU) && (sMsMode == MS_MODE_MAIN) &&
        (sMsCursor < MS_SLOT_COUNT)) {
        ms_draw_i8(fileSelect->state.gfxCtx, &gfx, gFileSelBigButtonHighlightTex,
                   72, 24, left - 4, slotHighlightTop, fileSelect->highlightColor[3],
                   fileSelect->highlightColor[0], fileSelect->highlightColor[1], fileSelect->highlightColor[2]);
    } else if ((fileSelect->configMode == CM_MAIN_MENU) && (sMsMode == MS_MODE_MAIN) &&
               (sMsCursor >= MS_MAIN_COPY)) {
        const u8* highlight = sMsEnglish ? gFileSelBigButtonHighlightTex
                                         : gFileSelMediumButtonHighlightTex;
        s16 highlightWidth = sMsEnglish ? 72 : 56;
        ms_draw_i8(fileSelect->state.gfxCtx, &gfx, highlight,
                   highlightWidth, 24, ms_action_button_left(left, sMsCursor - MS_MAIN_COPY) - 4,
                   buttonTop + 4,
                   fileSelect->highlightColor[3], fileSelect->highlightColor[0],
                   fileSelect->highlightColor[1], fileSelect->highlightColor[2]);
    } else if ((sMsMode == MS_MODE_COPY_SOURCE) || (sMsMode == MS_MODE_COPY_DEST) ||
               (sMsMode == MS_MODE_ERASE_SELECT)) {
        ms_draw_i8(fileSelect->state.gfxCtx, &gfx, gFileSelBigButtonHighlightTex,
                   72, 24, left - 4, slotHighlightTop, fileSelect->highlightColor[3],
                   fileSelect->highlightColor[0], fileSelect->highlightColor[1], fileSelect->highlightColor[2]);
    } else if ((sMsMode == MS_MODE_COPY_CONFIRM) || (sMsMode == MS_MODE_ERASE_CONFIRM)) {
        const u8* highlight = sMsEnglish ? gFileSelBigButtonHighlightTex
                                         : gFileSelMediumButtonHighlightTex;
        s16 highlightWidth = sMsEnglish ? 72 : 56;
        ms_draw_i8(fileSelect->state.gfxCtx, &gfx, highlight,
                   highlightWidth, 24, ms_confirm_button_left(left, sMsConfirm) - 4,
                   buttonTop + 4,
                   fileSelect->highlightColor[3], fileSelect->highlightColor[0],
                   fileSelect->highlightColor[1], fileSelect->highlightColor[2]);
    }
    POLY_OPA_DISP = gfx;
    CLOSE_DISPS(fileSelect->state.gfxCtx);
}

static void ms_draw_mapped_file_labels(FileSelectState* fileSelect) {
    Gfx* gfx;
    s16 backend = sMsActiveLogicalSlot / 2;
    s16 physical;
    s16 left = fileSelect->windowPosX - 6;

    OPEN_DISPS(fileSelect->state.gfxCtx);
    gfx = POLY_OPA_DISP;
    if (fileSelect->menuMode == FS_MENU_MODE_SELECT) {
        physical = sMsActiveLogicalSlot & 1;
        s16 top = 44 - (physical * 16) + fileSelect->buttonYOffsets[physical];

        ms_draw_slot_button(fileSelect, &gfx, sMsActiveLogicalSlot, left, top,
                            fileSelect->fileButtonAlpha[physical]);
        if (sMsSlots[sMsActiveLogicalSlot].occupied) {
            ms_draw_ia8(fileSelect->state.gfxCtx, &gfx, gFileSelConnectorTex, 24, 16,
                        left + 52, top, fileSelect->connectorAlpha[physical],
                        fileSelect->windowColor[0], fileSelect->windowColor[1],
                        fileSelect->windowColor[2]);
        }
    } else {
        for (physical = 0; physical < 2; physical++) {
            s16 alpha = fileSelect->fileButtonAlpha[physical];
            s16 top = 44 - (physical * 16) + fileSelect->buttonYOffsets[physical];
            const u16* buttonTexture = (const u16*)((physical & 1) ? gFileSelFile2ButtonENGTex
                                                                   : gFileSelFile1ButtonENGTex);
            ms_draw_ia16(fileSelect->state.gfxCtx, &gfx, buttonTexture, 64, 16,
                         left, top, alpha, fileSelect->windowColor[0],
                         fileSelect->windowColor[1], fileSelect->windowColor[2]);
            ms_draw_slot_label(fileSelect, &gfx, (backend * 2) + physical, left, top, alpha);
        }
    }
    POLY_OPA_DISP = gfx;
    CLOSE_DISPS(fileSelect->state.gfxCtx);
}

RECOMP_HOOK("FileSelect_Init") void ms_file_select_init(GameState* thisx) {
    sMsFileSelect = (FileSelectState*)thisx;
    sMsMode = MS_MODE_MAIN;
    sMsCursor = 0;
    sMsSource = -1;
    sMsDest = -1;
    sMsConfirm = 0;
    sMsActiveLogicalSlot = -1;
    sMsCurrentBackend = -1;
    ms_change_backend(0);
}

RECOMP_HOOK_RETURN("FileSelect_Init") void ms_file_select_init_return(void) {
    if (sMsFileSelect != NULL) {
        ms_scan_all(sMsFileSelect);
    }
}

RECOMP_HOOK("FileSelect_UpdateMainMenu") void ms_update_main_menu(GameState* thisx) {
    FileSelectState* fileSelect = (FileSelectState*)thisx;
    Input* input = CONTROLLER1(&fileSelect->state);
    u16 pressed = input->press.button;

    fileSelect->buttonIndex = 0;
    sMsMode = MS_MODE_MAIN;
    if (CHECK_BTN_ANY(pressed, BTN_A | BTN_START)) {
        if (sMsCursor < MS_SLOT_COUNT) {
            ms_open_slot(fileSelect, sMsCursor);
        } else if (sMsCursor == MS_MAIN_COPY) {
            if ((ms_count_occupied() == 0) || (ms_count_occupied() == MS_SLOT_COUNT)) {
                ms_play_error();
            } else {
                Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                ms_enter_mode(fileSelect, MS_MODE_COPY_SOURCE, 0, FS_TITLE_COPY_FROM);
            }
        } else if (sMsCursor == MS_MAIN_ERASE) {
            if (ms_count_occupied() == 0) {
                ms_play_error();
            } else {
                Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                ms_enter_mode(fileSelect, MS_MODE_ERASE_SELECT, 0, FS_TITLE_ERASE_FILE);
            }
        } else {
            input->press.button = BTN_B;
            return;
        }
        input->press.button &= ~(BTN_A | BTN_START);
        fileSelect->stickAdjX = 0;
        fileSelect->stickAdjY = 0;
        return;
    } else if (CHECK_BTN_ANY(pressed, BTN_B)) {
        return;
    } else {
        if (ABS_ALT(fileSelect->stickAdjY) > 30) {
            ms_play_cursor();
            if (sMsCursor < MS_SLOT_COUNT) {
                if (fileSelect->stickAdjY > 30) {
                    sMsCursor = (sMsCursor == 0) ? MS_MAIN_QUIT : sMsCursor - 1;
                } else {
                    sMsCursor = (sMsCursor == (MS_SLOT_COUNT - 1)) ? MS_MAIN_COPY : sMsCursor + 1;
                }
            } else if (fileSelect->stickAdjY > 30) {
                sMsCursor = MS_SLOT_COUNT - 1;
            } else {
                sMsCursor = 0;
            }
            fileSelect->stickAdjX = 0;
            fileSelect->stickAdjY = 0;
        } else if ((sMsCursor >= MS_MAIN_COPY) && (ABS_ALT(fileSelect->stickAdjX) > 30)) {
            ms_play_cursor();
            if (fileSelect->stickAdjX < -30) {
                sMsCursor = (sMsCursor == MS_MAIN_COPY) ? MS_MAIN_QUIT : sMsCursor - 1;
            } else {
                sMsCursor = (sMsCursor == MS_MAIN_QUIT) ? MS_MAIN_COPY : sMsCursor + 1;
            }
            fileSelect->stickAdjX = 0;
            fileSelect->stickAdjY = 0;
        }
    }
}

RECOMP_PATCH void FileSelect_UnusedCM31(GameState* thisx) {
    FileSelectState* fileSelect = (FileSelectState*)thisx;
    Input* input = CONTROLLER1(&fileSelect->state);

    fileSelect->buttonIndex = 0;
    if ((sMsMode == MS_MODE_COPY_DONE) || (sMsMode == MS_MODE_ERASE_DONE)) {
        sMsDoneTimer--;
        if (sMsDoneTimer <= 0) {
            ms_return_main(fileSelect, (sMsMode == MS_MODE_COPY_DONE) ? sMsDest : sMsSource);
        }
        return;
    }

    if (CHECK_BTN_ANY(input->press.button, BTN_B)) {
        Audio_PlaySfx(NA_SE_SY_FSEL_CLOSE);
        if (sMsMode == MS_MODE_COPY_DEST) {
            ms_enter_mode(fileSelect, MS_MODE_COPY_SOURCE, sMsSource, FS_TITLE_COPY_FROM);
        } else if (sMsMode == MS_MODE_COPY_CONFIRM) {
            ms_enter_mode(fileSelect, MS_MODE_COPY_DEST, sMsDest, FS_TITLE_COPY_TO);
        } else if (sMsMode == MS_MODE_ERASE_CONFIRM) {
            ms_enter_mode(fileSelect, MS_MODE_ERASE_SELECT, sMsSource, FS_TITLE_ERASE_FILE);
        } else {
            ms_return_main(fileSelect, (sMsMode == MS_MODE_ERASE_SELECT) ? MS_MAIN_ERASE : MS_MAIN_COPY);
        }
        return;
    }

    if ((sMsMode == MS_MODE_COPY_CONFIRM) || (sMsMode == MS_MODE_ERASE_CONFIRM)) {
        ms_update_confirm(fileSelect);
        if (CHECK_BTN_ANY(input->press.button, BTN_A | BTN_START)) {
            if (sMsConfirm != 0) {
                Audio_PlaySfx(NA_SE_SY_FSEL_CLOSE);
                if (sMsMode == MS_MODE_COPY_CONFIRM) {
                    ms_enter_mode(fileSelect, MS_MODE_COPY_DEST, sMsDest, FS_TITLE_COPY_TO);
                } else {
                    ms_enter_mode(fileSelect, MS_MODE_ERASE_SELECT, sMsSource, FS_TITLE_ERASE_FILE);
                }
            } else if (sMsMode == MS_MODE_COPY_CONFIRM) {
                if (!ms_copy_slot(fileSelect, sMsSource, sMsDest)) {
                    ms_play_error();
                    ms_enter_mode(fileSelect, MS_MODE_COPY_DEST, sMsDest, FS_TITLE_COPY_TO);
                } else {
                    Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                    sMsMode = MS_MODE_COPY_DONE;
                    sMsDoneTimer = 30;
                    fileSelect->titleLabel = FS_TITLE_COPY_COMPLETE;
                }
            } else {
                ms_erase_slot(fileSelect, sMsSource);
                Audio_PlaySfx(NA_SE_EV_DIAMOND_SWITCH);
                sMsMode = MS_MODE_ERASE_DONE;
                sMsDoneTimer = 30;
                fileSelect->titleLabel = FS_TITLE_ERASE_COMPLETE;
            }
        }
        return;
    }

    ms_move_slot_cursor(fileSelect);
    if (!CHECK_BTN_ANY(input->press.button, BTN_A | BTN_START)) {
        return;
    }

    if (sMsMode == MS_MODE_COPY_SOURCE) {
        if (!sMsSlots[sMsCursor].occupied) {
            ms_play_error();
        } else {
            Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
            sMsSource = sMsCursor;
            ms_enter_mode(fileSelect, MS_MODE_COPY_DEST, (sMsCursor + 1) % MS_SLOT_COUNT, FS_TITLE_COPY_TO);
        }
    } else if (sMsMode == MS_MODE_COPY_DEST) {
        if ((sMsCursor == sMsSource) || sMsSlots[sMsCursor].occupied) {
            ms_play_error();
        } else {
            Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
            sMsDest = sMsCursor;
            sMsConfirm = 1;
            ms_enter_mode(fileSelect, MS_MODE_COPY_CONFIRM, sMsCursor, FS_TITLE_COPY_CONFIRM);
        }
    } else if (sMsMode == MS_MODE_ERASE_SELECT) {
        if (!sMsSlots[sMsCursor].occupied) {
            ms_play_error();
        } else {
            Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
            sMsSource = sMsCursor;
            sMsConfirm = 1;
            ms_enter_mode(fileSelect, MS_MODE_ERASE_CONFIRM, sMsCursor, FS_TITLE_ERASE_CONFIRM);
        }
    }
}

RECOMP_HOOK_RETURN("FileSelect_RotateToMain") void ms_rotate_to_main_return(void) {
    if ((sMsFileSelect != NULL) && (sMsFileSelect->configMode == CM_MAIN_MENU)) {
        s16 cursor = (sMsActiveLogicalSlot >= 0) ? sMsActiveLogicalSlot : sMsCursor;
        if ((sMsActiveLogicalSlot >= 0) &&
            ms_slot_occupied(sMsFileSelect, sMsActiveLogicalSlot & 1)) {
            ms_refresh_backend(sMsFileSelect, sMsActiveLogicalSlot / 2);
        } else {
            ms_capture_backend(sMsFileSelect, 0);
        }
        sMsActiveLogicalSlot = -1;
        ms_return_main(sMsFileSelect, cursor);
    }
}

RECOMP_HOOK_RETURN("FileSelect_MoveSelectedFileToSlot") void ms_move_to_slot_return(void) {
    if ((sMsFileSelect != NULL) && (sMsFileSelect->menuMode == FS_MENU_MODE_CONFIG) &&
        (sMsFileSelect->configMode == CM_MAIN_MENU) && (sMsActiveLogicalSlot >= 0)) {
        s16 cursor = (sMsActiveLogicalSlot >= 0) ? sMsActiveLogicalSlot : sMsCursor;
        ms_refresh_backend(sMsFileSelect, sMsActiveLogicalSlot / 2);
        sMsActiveLogicalSlot = -1;
        ms_return_main(sMsFileSelect, cursor);
    }
}

RECOMP_HOOK("FileSelect_DrawWindowContents") void ms_draw_window_contents(GameState* thisx) {
    FileSelectState* fileSelect = (FileSelectState*)thisx;
    s32 i;
    s32 customSelect;

    sMsFileSelect = fileSelect;
    sMsDrawFullUi = (fileSelect->menuMode == FS_MENU_MODE_CONFIG) &&
                    ((fileSelect->configMode == CM_FADE_IN_START) ||
                     (fileSelect->configMode == CM_FADE_IN_END) ||
                     (fileSelect->configMode == CM_MAIN_MENU) ||
                     (fileSelect->configMode == CM_UNUSED_31));
    sMsDrawMappedLabels = !sMsDrawFullUi && (sMsActiveLogicalSlot >= 0) &&
                          ((fileSelect->menuMode == FS_MENU_MODE_SELECT) ||
                           (fileSelect->configMode == CM_ROTATE_TO_NAME_ENTRY) ||
                           (fileSelect->configMode == CM_NAME_ENTRY_TO_MAIN));
    customSelect = (sMsActiveLogicalSlot >= 0) &&
                   (fileSelect->menuMode == FS_MENU_MODE_SELECT);
    if ((fileSelect->windowContentVtx == NULL) ||
        (!sMsDrawFullUi && !sMsDrawMappedLabels && !customSelect)) {
        return;
    }
    if (sMsDrawFullUi) {
        for (i = 4; i < 960; i++) {
            fileSelect->windowContentVtx[i].v.ob[0] += 1000;
        }
    } else if (customSelect) {
        for (i = 88; i < 136; i++) {
            fileSelect->windowContentVtx[i].v.ob[0] += 1000;
        }
    } else {
        for (i = 88; i < 92; i++) {
            fileSelect->windowContentVtx[i].v.ob[0] += 1000;
        }
        for (i = 104; i < 108; i++) {
            fileSelect->windowContentVtx[i].v.ob[0] += 1000;
        }
    }
}

RECOMP_HOOK_RETURN("FileSelect_DrawWindowContents") void ms_draw_window_contents_return(void) {
    if (sMsFileSelect == NULL) {
        return;
    }
    if (sMsDrawFullUi) {
        ms_draw_full_ui(sMsFileSelect);
    } else if (sMsDrawMappedLabels) {
        ms_draw_mapped_file_labels(sMsFileSelect);
    }
}

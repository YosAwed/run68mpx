/* $Id: disassemble.c,v 1.3 2009-08-08 06:49:44 masamic Exp $ */

/*
 * $Log: not supported by cvs2svn $
 * Revision 1.2  2009/08/05 14:44:33  masamic
 * Some Bug fix, and implemented some instruction
 * Following Modification contributed by TRAP.
 *
 * Fixed Bug: In disassemble.c, shift/rotate as{lr},ls{lr},ro{lr} alway show word size.
 * Modify: enable KEYSNS, register behaiviour of sub ea, Dn.
 * Add: Nbcd, Sbcd.
 *
 * Revision 1.1.1.1  2001/05/23 11:22:06  masamic
 * First imported source code and docs
 *
 * Revision 1.5  2000/01/09  04:22:42  yfujii
 * Automaton for making register list is fixed for buginfo0002.
 *
 * Revision 1.4  1999/12/23  08:08:16  yfujii
 * Wrong instruction strings generated for some instructions, are fixed.
 *
 * Revision 1.3  1999/12/07  12:41:31  yfujii
 * *** empty log message ***
 *
 * Revision 1.3  1999/11/30  13:27:38  yfujii
 * Wrong interpretation of 'DBcc' instruction is fixed.
 * Wrong interpretation of 'MOVE #$xxxx,$yyyyyy' is fixed.
 *
 * Revision 1.2  1999/11/22  03:58:02  yfujii
 * Wrong treatment of 'movem' instruction is fixed.
 *
 * Revision 1.1  1999/11/01  06:22:26  yfujii
 * Initial revision
 *
 */

#include <assert.h>
#include "run68.h"

/* prog_ptr_uは符号付きcharで不便なので、符号なしcharに変換しておく。*/
#define prog_ptr_u ((unsigned char *)prog_ptr)

static char *disa0(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa1_2_3(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa4(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa5(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa6(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa7(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa8(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disa9_d(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disab(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disac(Long addr, unsigned short code, Long *next_addr, char *mnemonic);
static char *disae(Long addr, unsigned short code, Long *next_addr, char *mnemonic);

/*
   機能：
     指定したアドレスから始まるMPU命令を文字列に変換する。
   パラメータ：
     Long  addr      <in>  命令のアドレス
     Long *next_addr <out> 次の命令のアドレス
   戻り値：
*/

char *disassemble(Long addr, Long* next_addr)
{
    static char mnemonic[64], *ptr;
    unsigned short code;

    ptr = NULL;
    *next_addr = addr;
    mnemonic[0] = '\0';
    code = (((unsigned short)prog_ptr_u[addr]) << 8) + (unsigned short)prog_ptr_u[addr+1];
    switch((code & 0xf000) >> 12)
    {
    case 0x0:
        ptr = disa0(addr, code, next_addr, mnemonic);
        break;
    case 0x1:
    case 0x2:
    case 0x3:
        ptr = disa1_2_3(addr, code, next_addr, mnemonic);
        break;
    case 0x4:
        ptr = disa4(addr, code, next_addr, mnemonic);
        break;
    case 0x5:
        ptr = disa5(addr, code, next_addr, mnemonic);
        break;
    case 0x6:
        ptr = disa6(addr, code, next_addr, mnemonic);
        break;
    case 0x7:
        ptr = disa7(addr, code, next_addr, mnemonic);
        break;
    case 0x8:
        ptr = disa8(addr, code, next_addr, mnemonic);
        break;
    case 0x9:
    case 0xd:
        ptr = disa9_d(addr, code, next_addr, mnemonic);
        break;
    case 0xb:
        ptr = disab(addr, code, next_addr, mnemonic);
        break;
    case 0xc:
        ptr = disac(addr, code, next_addr, mnemonic);
        break;
    case 0xe:
        ptr = disae(addr, code, next_addr, mnemonic);
        break;
    case 0xf:
        switch(code & 0x0f00)
        {
        case 0x0f00:
            snprintf(mnemonic, 64, "FCALL $%02X", code & 0xff);
            ptr = mnemonic;
            break;
        case 0x0e00:
            snprintf(mnemonic, 64, "FLOAT $%02X", code & 0xff);
            ptr = mnemonic;
            break;
        }
        *next_addr = addr + 2;
    }
    if (ptr == NULL)
    {
        return NULL;
    }
    return ptr;
}

static BOOL effective_address(Long addr, short mode, short reg, char size,
                              unsigned short mask, char *str, size_t str_size,
                              Long *next_addr);

static size_t remaining_text(const char *base, size_t capacity,
                             const char *position)
{
    size_t used = (size_t)(position - base);
    return used < capacity ? capacity - used : 0;
}

static void append_text(char *destination, size_t capacity,
                        const char *source)
{
    size_t used = strnlen(destination, capacity);
    if (used < capacity)
        snprintf(destination + used, capacity - used, "%s", source);
}

static void fill_space(char *str, unsigned int n)
{
    unsigned int i;
    if (strlen(str) >= n)
        return;
    for (i = strlen(str); i < n; i ++)
    {
        str[i] = ' ';
    }
    str[n] = '\0';
}

static char *disa0(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char *p;
    ULong d1;
    char size = '?';

    /* まずは全ビット固定の命令を調べる */
    switch(code)
    {
    case 0x003c:  /* OR Immediate to CCR */
        snprintf(mnemonic, 64, "%s", "or");
        goto L0;
    case 0x023c:  /* AND Immediate to CCR */
        snprintf(mnemonic, 64, "%s", "and");
        goto L0;
    case 0x0a3c:  /* XOR Immediate to CCR */
        snprintf(mnemonic, 64, "%s", "xor");
L0:
        d1 = (unsigned short)prog_ptr_u[addr + 3];
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        snprintf(p, remaining_text(mnemonic, 64, p), "#$%02x,ccr", d1);
        *next_addr = addr + 4;
        return mnemonic;
    case 0x007c:  /* OR Immediate to SR */
        snprintf(mnemonic, 64, "%s", "or");
        goto L1;
    case 0x027c:  /* AND Immediate to SR */
        snprintf(mnemonic, 64, "%s", "and");
        goto L1;
    case 0x0a7c:  /* EOR Immediate to SR */
        snprintf(mnemonic, 64, "%s", "eor");
L1:
        d1 = ((unsigned short)prog_ptr_u[addr + 2] << 8) + (unsigned short)prog_ptr_u[addr + 3];
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        snprintf(p, remaining_text(mnemonic, 64, p), "#$%04x,sr", d1);
        *next_addr = addr + 4;
        goto EndOfFunc;
    }
    /* 次に、上位8ビットのみ固定の命令を調べる */
    switch(code & 0xff00)
    {
    case 0x0000:  /* OR Immediate */
        snprintf(mnemonic, 64, "%s", "or");
        goto L2;
    case 0x0200:  /* AND Immediate */
        snprintf(mnemonic, 64, "%s", "and");
        goto L2;
    case 0x0400:  /* SUB Immediate */
        snprintf(mnemonic, 64, "%s", "sub");
        goto L2;
    case 0x0600:  /* ADD Immediate */
        snprintf(mnemonic, 64, "%s", "add");
        goto L2;
    case 0x0800:  /* Static Bit Operations */
        switch (code & 0x00c0)
        {
        case 0x0000:
            snprintf(mnemonic, 64, "%s", "btst");
            break;
        case 0x0040:
            snprintf(mnemonic, 64, "%s", "bchg");
            break;
        case 0x0080:
            snprintf(mnemonic, 64, "%s", "bclr");
            break;
        case 0x00c0:
            snprintf(mnemonic, 64, "%s", "bset");
        }
        *next_addr = addr + 4;
        d1 = ((unsigned short)prog_ptr_u[addr + 2] << 8) + (unsigned short)prog_ptr_u[addr + 3];
        size = ' ';  /* Data registers are Long only. Others are byte only. */
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        snprintf(p, remaining_text(mnemonic, 64, p), "#%d,", d1);
        p = mnemonic + strlen(mnemonic);
        goto AddEA;
    case 0x0a00:  /* EOR Immediate */
        snprintf(mnemonic, 64, "%s", "eor");
        goto L2;
    case 0x0c00:  /* CMP Immediate */
        snprintf(mnemonic, 64, "%s", "cmp");
L2:
        switch (code & 0x00c0)
        {
        case 0x0000:
            append_text(mnemonic, 64, ".b");
            *next_addr = addr + 4;
            d1 = (unsigned short)prog_ptr_u[addr + 3];
            size = 'b';
            break;
        case 0x0040:
            append_text(mnemonic, 64, ".w");
            *next_addr = addr + 4;
            d1 = ((unsigned short)prog_ptr_u[addr + 2] << 8) + (unsigned short)prog_ptr_u[addr + 3];
            size = 'w';
            break;
        case 0x0080:
            append_text(mnemonic, 64, ".l");
            *next_addr = addr + 6;
            d1 = ((ULong)prog_ptr_u[addr + 2] << 24) + ((ULong)prog_ptr_u[addr + 3] << 16)
               + ((ULong)prog_ptr_u[addr + 4] << 8) + (ULong)prog_ptr_u[addr + 5];
            size = 'l';
            break;
        case 0x00c0:
            /* サイズ不明 */
            goto ErrorReturn;
        }
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        switch(size)
        {
        case 'b':
            snprintf(p, remaining_text(mnemonic, 64, p), "#$%02x,", d1);
            break;
        case 'w':
            snprintf(p, remaining_text(mnemonic, 64, p), "#$%04x,", d1);
            break;
        case 'l':
            snprintf(p, remaining_text(mnemonic, 64, p), "#$%08x,", d1);
            break;
        default:
            /* 命令デコードエラー */
            goto ErrorReturn;
        }
        goto AddEA;
    }
    /* 残った命令を拾う(二つある) */
    if (code & 0x0100)
    {
        /* Dynamic Bit Operation */
        switch (code & 0x00c0)
        {
        case 0x0000:
            append_text(mnemonic, 64, "btst");
            break;
        case 0x0040:
            append_text(mnemonic, 64, "bchg");
            break;
        case 0x0080:
            snprintf(mnemonic, 64, "%s", "bclr");
            break;
        case 0x00c0:
            append_text(mnemonic, 64, "bset");
        }
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        snprintf(p, remaining_text(mnemonic, 64, p), "d%01d,", (code & 0x0e00) >> 9);
        *next_addr = addr + 2;
        size = ' ';  /* Data registers are Long only. Others are byte only. */
        goto AddEA;
    } else if ((code & 0x0038) == 0x0008)
    {
        /* MOVEP命令 */
        append_text(mnemonic, 64, "movep");
        switch((code & 0x1c0) >> 6)
        {
        case 0x04:
            append_text(mnemonic, 64, ".w");
            goto L4;
        case 0x05:
            append_text(mnemonic, 64, ".l");
L4:
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            d1 = ((unsigned short)prog_ptr_u[addr + 2] << 8) + (unsigned short)prog_ptr_u[addr + 3];
            snprintf(p, remaining_text(mnemonic, 64, p), "%d(a%1d),d%1d", d1, code & 0x07, (code & 0x0e00) >> 9);
            *next_addr = addr + 4;
            goto EndOfFunc;
        case 0x06:
            append_text(mnemonic, 64, ".w");
            goto L5;
        case 0x07:
            append_text(mnemonic, 64, ".l");
L5:
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            d1 = ((unsigned short)prog_ptr_u[addr + 2] << 8) + (unsigned short)prog_ptr_u[addr + 3];
            snprintf(p, remaining_text(mnemonic, 64, p), "d%1d,%d(a%1d)", (code & 0x0e00) >> 9, d1, code & 0x07);
            *next_addr = addr + 4;
            goto EndOfFunc;
        default:
            goto ErrorReturn;
        }
        goto EndOfFunc;
    } else
    {
        goto ErrorReturn;
    }
AddEA:
    p = &mnemonic[strlen(mnemonic)];
    /* 即値は有り得ないのでデータサイズには' 'を与える。*/
    effective_address(*next_addr, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

/*
   機能：
   パラメータ：
     Long   addr       <in>  オペランドのアドレス(使うとは限らない)
     ushort mode       <in>  実効アドレスのモードフィールド(0-7)
     ushort reg        <in>  実効アドレスのレジスタフィールド(0-7)
     char   size       <in>  即値の場合のデータサイズ('b'/'w'/'l')
     ushort mask       <in>  無効なモードをビット位置の1により指定
     char   *str       <out> 実効アドレスを文字列にして書き込む
     Long   *next_addr <out> 次のオペランドまたは命令のアドレス
   戻り値：
     BOOL   FALSEならエラー。エラー時もnext_addrは有効。
*/
static BOOL effective_address(Long addr, short mode, short reg, char size,
                              unsigned short mask, char *str, size_t str_size,
                              Long *next_addr)
{
    short disp, ext;
    unsigned short absw;
    ULong  absl;
    ULong  imm;

    switch(mode)
    {
    case 0:  /* パターン0:データレジスタ直接 */
        snprintf(str, str_size, "d%1d", reg);
        *next_addr = addr;
        break;
    case 1:  /* パターン1:アドレスレジスタ直接 */
        snprintf(str, str_size, "a%1d", reg);
        *next_addr = addr;
        break;
    case 2:  /* パターン2:アドレスレジスタ間接 */
        snprintf(str, str_size, "(a%1d)", reg);
        *next_addr = addr;
        break;
    case 3:  /* パターン3:ポストインクリメント付きアドレスレジスタ間接 */
        snprintf(str, str_size, "(a%1d)+", reg);
        *next_addr = addr;
        break;
    case 4:  /* パターン4:プリデクリメント付きアドレスレジスタ間接 */
        snprintf(str, str_size, "-(a%1d)", reg);
        *next_addr = addr;
        break;
    case 5:  /* パターン5:ディスプレースメント付きアドレスレジスタ間接 */
        /* ディスプレースメントは符号付きのワード値である */
        disp = (short)((unsigned short)prog_ptr_u[addr] << 8) + (unsigned short)prog_ptr_u[addr + 1];
        snprintf(str, str_size, "%d(a%1d)", disp, reg);
        *next_addr = addr + 2;
        break;
    case 6:  /* パターン6:インデックス付きアドレスレジスタ間接 */
        ext = (short)((unsigned short)prog_ptr_u[addr] << 8) + (unsigned short)prog_ptr_u[addr + 1];
        snprintf(str, str_size, "%d(a%1d,%c%1d.%c)", (signed char)(ext & 0xff),
                reg, ext & 0x8000?'a':'d',
                (ext & 0x7000) >> 12, ext & 0x0800?'l':'w');
        *next_addr = addr + 2;
        break;
    case 7:  /* regフィールドで更に場合分け */
        switch(reg)
        {
        case 0x0: /* パターン7:絶対ショートアドレス */
            absw = ((unsigned short)prog_ptr_u[addr] << 8)
                 + (unsigned short)prog_ptr_u[addr + 1];
            snprintf(str, str_size, "$%06x", absw);
            *next_addr = addr + 2;
            break;
        case 0x1: /* パターン8:絶対ロングアドレス */
            absl = ((ULong)prog_ptr_u[addr] << 24)
                 + ((ULong)prog_ptr_u[addr + 1] << 16)
                 + ((ULong)prog_ptr_u[addr + 2] << 8)
                 + (ULong)prog_ptr_u[addr + 3];
            snprintf(str, str_size, "$%06x", absl);
            *next_addr = addr + 4;
            break;
        case 0x2: /* パターン9:ディスプレースメント付きPC相対 */
            /* ディスプレースメントは符号付きのワード値である */
            disp = (short)((unsigned short)prog_ptr_u[addr] << 8) + (unsigned short)prog_ptr_u[addr + 1];
            snprintf(str, str_size, "%d(pc)", disp);
            *next_addr = addr + 2;
            break;
        case 0x3: /* パターン10:インデックス付きPC相対 */
            ext = (short)((unsigned short)prog_ptr_u[addr] << 8) + (unsigned short)prog_ptr_u[addr + 1];
            snprintf(str, str_size, "%d(pc,%c%1d.%c)", (signed char)(ext & 0xff),
                    ext & 0x8000?'a':'d',
                    (ext & 0x7000) >> 12, ext & 0x0800?'l':'w');
            *next_addr = addr + 2;
            break;
        case 0x4: /* パターン11:即値(またはステータスレジスタ) */
            /* ステータスレジスタの場合はここには現れない */
            switch(size)
            {
            case 'b':
                imm = (ULong)prog_ptr_u[addr + 1];
                *next_addr = addr + 2;
                break;
            case 'w':
                imm = ((ULong)prog_ptr_u[addr] << 8)
                    + (ULong)prog_ptr_u[addr + 1];
                *next_addr = addr + 2;
                break;
            case 'l':
                imm = ((ULong)prog_ptr_u[addr] << 24)
                    + ((ULong)prog_ptr_u[addr + 1] << 16)
                    + ((ULong)prog_ptr_u[addr + 2] << 8)
                    + (ULong)prog_ptr_u[addr + 3];
                *next_addr = addr + 4;
                break;
            default:
                /* ここには来ないはず。*/
                goto ErrorReturn;
            }
            snprintf(str, str_size, "#$%x", imm);
            break;
        default: /* 存在しないアドレッシングモード */
            *next_addr = addr;
            goto ErrorReturn;
        }
    }
    return TRUE;
ErrorReturn:
    return FALSE;
}

static char *disa1_2_3(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char dstr[64], sstr[64];
    BOOL b;
    char size = ' ';

    if ((code & 0x1c0) == 0x40)
    {
        /* アドレスレジスタがデスティネーションの場合は"movea"とする。*/
        switch(code & 0xf000)
        {
        case 0x2000:
            append_text(mnemonic, 64, "movea.l");
            size = 'l';
            break;
        case 0x3000:
            append_text(mnemonic, 64, "movea.w");
            size = 'w';
            break;
        }
    } else
    {
        switch(code & 0xf000)
        {
        case 0x1000:
            append_text(mnemonic, 64, "move.b");
            size = 'b';
            break;
        case 0x2000:
            append_text(mnemonic, 64, "move.l");
            size = 'l';
            break;
        case 0x3000:
            append_text(mnemonic, 64, "move.w");
            size = 'w';
            break;
        }
    }
    fill_space(mnemonic, 8);
    b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, sstr, sizeof(sstr), &addr);
    if (b == FALSE)
        goto ErrorReturn;
    b = effective_address(addr, (short)((code & 0x1c0) >> 6), (short)((code & 0xe00) >> 9), size, 0xfff, dstr, sizeof(dstr), next_addr);
    if (b == FALSE)
        goto ErrorReturn;
    append_text(mnemonic, 64, sstr);
    append_text(mnemonic, 64, ",");
    append_text(mnemonic, 64, dstr);
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disa4(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    signed short disp;
    char reg[10], *p, size;
    BOOL b;
    short stat, dstat;
    unsigned short regmask;
    int i;

    *next_addr = addr + 2;
    /* まずは全ビット固定の命令を処理する */
    switch(code)
    {
    case 0x4afc:
        append_text(mnemonic, 64,"illegal");
        goto EndOfFunc;
    case 0x4e70:
        append_text(mnemonic, 64,"reset");
        goto EndOfFunc;
    case 0x4e71:
        append_text(mnemonic, 64,"nop");
        goto EndOfFunc;
    case 0x4e72:
        append_text(mnemonic, 64,"stop");
        goto EndOfFunc;
    case 0x4e73:
        append_text(mnemonic, 64,"rte");
        goto EndOfFunc;
    case 0x4e75:
        append_text(mnemonic, 64,"rts");
        goto EndOfFunc;
    case 0x4e76:
        append_text(mnemonic, 64,"trapv");
        goto EndOfFunc;
    case 0x4e77:
        append_text(mnemonic, 64,"rtr");
        goto EndOfFunc;
    }
    /* 次に、13ビット固定の命令を処理する */
    switch(code & 0xfff8)
    {
    case 0x4840:
        snprintf(mnemonic, 64, "swap    d%1d", code & 0x7);
        goto EndOfFunc;
    case 0x4880:
        snprintf(mnemonic, 64, "ext.w   d%1d", code & 0x7);
        goto EndOfFunc;
    case 0x48c0:
        snprintf(mnemonic, 64, "ext.l   d%1d", code & 0x7);
        goto EndOfFunc;
    case 0x4e50:
        disp = (signed short)(((unsigned short)prog_ptr_u[addr + 2] << 8)
                    + (unsigned short)prog_ptr_u[addr + 3]);
        snprintf(mnemonic, 64, "link    a%1d,#%d", code & 0x7, disp);
        *next_addr += 2;
        goto EndOfFunc;
    case 0x4e58:
        snprintf(mnemonic, 64, "unlk    a%1d", code & 0x7);
        goto EndOfFunc;
    case 0x4e60:
        snprintf(mnemonic, 64, "move    a%1d,usp", code & 0x7);
        goto EndOfFunc;
    case 0x4e68:
        snprintf(mnemonic, 64, "move    usp,a%1d", code & 0x7);
        goto EndOfFunc;
    }
    /* 次に、12ビット固定の命令を処理する */
    switch(code & 0xfff0)
    {
    case 0x4e40:
        snprintf(mnemonic, 64, "trap    #%d", code & 0xf);
        goto EndOfFunc;
    }
    /* 次に、10ビット固定の命令を処理する */
    switch(code & 0xffc0)
    {
    case 0x40c0:
        append_text(mnemonic, 64, "move.w  sr,");
        size = 'w';
        goto AddEA;
    case 0x44c0:
        append_text(mnemonic, 64, "move.w  ");
        size = 'w';
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr+2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        append_text(mnemonic, 64, ",ccr");
        if (b == FALSE)
            goto ErrorReturn;
        goto EndOfFunc;
    case 0x46c0:
        append_text(mnemonic, 64, "move.w  ");
        size = 'w';
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr+2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        append_text(mnemonic, 64, ",sr");
        if (b == FALSE)
            goto ErrorReturn;
        goto EndOfFunc;
    case 0x4800:
        append_text(mnemonic, 64, "nbcd    ");
        size = 'l';
        goto AddEA;
    case 0x4840:
        append_text(mnemonic, 64, "pea.l   ");
        size = 'l';
        goto AddEA;
    case 0x4ac0:
        append_text(mnemonic, 64, "tas.b   ");
        size = 'b';
        goto AddEA;
    case 0x4e80:
        append_text(mnemonic, 64, "jsr     ");
        size = 'l';
        goto AddEA;
    case 0x4ec0:
        append_text(mnemonic, 64, "jmp     ");
        size = 'l';
        goto AddEA;
    case 0x4000:
        append_text(mnemonic, 64, "negx.b  ");
        size = 'b';
        goto AddEA;
    case 0x4040:
        append_text(mnemonic, 64, "negx.w  ");
        size = 'w';
        goto AddEA;
    case 0x4080:
        append_text(mnemonic, 64, "negx.l  ");
        size = 'l';
        goto AddEA;
    case 0x4200:
        append_text(mnemonic, 64, "clr.b   ");
        size = 'b';
        goto AddEA;
    case 0x4240:
        append_text(mnemonic, 64, "clr.w   ");
        size = 'w';
        goto AddEA;
    case 0x4280:
        append_text(mnemonic, 64, "clr.l   ");
        size = 'l';
        goto AddEA;
    case 0x4400:
        append_text(mnemonic, 64, "neg.b   ");
        size = 'b';
        goto AddEA;
    case 0x4440:
        append_text(mnemonic, 64, "neg.w   ");
        size = 'w';
        goto AddEA;
    case 0x4480:
        append_text(mnemonic, 64, "neg.l   ");
        size = 'l';
        goto AddEA;
    case 0x4600:
        append_text(mnemonic, 64, "not.b   ");
        size = 'b';
        goto AddEA;
    case 0x4640:
        append_text(mnemonic, 64, "not.w   ");
        size = 'w';
        goto AddEA;
    case 0x4680:
        append_text(mnemonic, 64, "not.l   ");
        size = 'l';
        goto AddEA;
    case 0x4a00:
        append_text(mnemonic, 64, "tst.b   ");
        size = 'b';
        goto AddEA;
    case 0x4a40:
        append_text(mnemonic, 64, "tst.w   ");
        size = 'w';
        goto AddEA;
    case 0x4a80:
        append_text(mnemonic, 64, "tst.l   ");
        size = 'l';
        goto AddEA;
    case 0x4c80: /* MOVEM */
        append_text(mnemonic, 64, "movem.w ");
        size = 'w';
        goto L0;
    case 0x4cc0:
        append_text(mnemonic, 64, "movem.l ");
        size = 'l';
L0:
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr+4, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        append_text(mnemonic, 64, ",");
        if (b == FALSE)
            goto ErrorReturn;
        goto L1;
    case 0x4880:
        append_text(mnemonic, 64, "movem.w ");
        size = 'w';
        goto L1;
    case 0x48c0:
        append_text(mnemonic, 64, "movem.l ");
        size = 'l';
L1:
        /* MOVEM命令のレジスタリストをAutomatonで文字列に変換する */
        regmask = ((unsigned short)prog_ptr_u[addr + 2] << 8)
              + (unsigned short)prog_ptr_u[addr + 3];
        /* データレジスタ */
        stat = 0;
        for (i = 0; i < 8; i ++)
        {
            unsigned short e;
            if ((code & 0x38) == 0x20)
            {
                /* プレデクリメントモードの時はレジスタの順序が逆 */
                e = regmask & (0x8000 >> i);
            } else
            {
                e = regmask & (1 << i);
            }
            switch(stat)
            {
            case 0:
                if (e == 0)
                {
                    /* nothing */
                } else
                {
                    stat = 1;
                    snprintf(reg, sizeof(reg), "d%1d", i);
                    append_text(mnemonic, 64, reg);
                }
                break;
            case 1:
                if (e == 0)
                {
                    stat = 3;
                } else
                {
                    stat = 2;
                }
                break;
            case 2:
                if (e == 0)
                {
                    stat = 3;
                    snprintf(reg, sizeof(reg), "-d%1d", i - 1);
                    append_text(mnemonic, 64, reg);
                } else
                {
                    /* nothing */
                }
                break;
            case 3:
                if (e == 0)
                {
                    /* nothing */
                } else
                {
                    stat = 1;
                    snprintf(reg, sizeof(reg), "/d%1d", i);
                    append_text(mnemonic, 64, reg);
                }
                break;
            }
        }
        if (stat == 2)
        {
            snprintf(reg, sizeof(reg), "-d%1d", i - 1);
            append_text(mnemonic, 64, reg);
        }
        dstat = stat;
        stat = 0;
        /* アドレスレジスタ */
        for (i = 8; i < 16; i ++)
        {
            unsigned short e;
            if ((code & 0x38) == 0x20)
            {
                /* プレデクリメントモードの時はレジスタの順序が逆 */
                e = regmask & (0x8000 >> i);
            } else
            {
                e = regmask & (1 << i);
            }
            switch(stat)
            {
            case 0:
                if (e == 0)
                {
                    /* nothing */
                } else
                {
                    stat = 1;
                    snprintf(reg, sizeof(reg), "a%1d", i - 8);
                    if (dstat != 0)
                        append_text(mnemonic, 64, "/");
                    append_text(mnemonic, 64, reg);
                }
                break;
            case 1:
                if (e == 0)
                {
                    stat = 3;
                } else
                {
                    stat = 2;
                }
                break;
            case 2:
                if (e == 0)
                {
                    stat = 3;
                    snprintf(reg, sizeof(reg), "-a%1d", i - 9);
                    append_text(mnemonic, 64, reg);
                } else
                {
                    /* nothing */
                }
                break;
            case 3:
                if (e == 0)
                {
                    /* nothing */
                } else
                {
                    stat = 1;
                    snprintf(reg, sizeof(reg), "/a%1d", i - 8);
                    append_text(mnemonic, 64, reg);
                }
                break;
            }
        }
        if (stat == 2)
        {
            snprintf(reg, sizeof(reg), "-a%1d", i - 9);
            append_text(mnemonic, 64, reg);
        }
        if ((code & 0x0400) == 0)
        {
            append_text(mnemonic, 64, ",");
            p = mnemonic + strlen(mnemonic);
            b = effective_address(addr + 4, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
            if (b == FALSE)
                goto ErrorReturn;
        }
        goto EndOfFunc;
    }
    /* 最後に、CHKとLEA命令を処理する */
    switch(code & 0xf1c0)
    {
    case 0x4180:
        append_text(mnemonic, 64, "chk.w   ");
        size = 'w';
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr+2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        p = mnemonic + strlen(mnemonic);
        if (b == FALSE)
            goto ErrorReturn;
        snprintf(p, remaining_text(mnemonic, 64, p), ",d%1d", (code & 0xe00) >> 9);
        goto EndOfFunc;
    case 0x41c0:
        append_text(mnemonic, 64, "lea.l   ");
        size = 'l';
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr+2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        p = mnemonic + strlen(mnemonic);
        if (b == FALSE)
            goto ErrorReturn;
        snprintf(p, remaining_text(mnemonic, 64, p), ",a%1d", (code & 0xe00) >> 9);
        goto EndOfFunc;
    default:
        goto ErrorReturn;
    }
    append_text(mnemonic, 64, "No instruction found.");
    goto ErrorReturn;
AddEA:
    p = mnemonic + strlen(mnemonic);
    b = effective_address(addr+2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
    if (b == FALSE)
        goto ErrorReturn;
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disa5(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char size = ' ';
    char *p;
    BOOL b;
    signed short offset;

    if ((code & 0xf8) == 0xc8)
    {
        /* DBcc */
        append_text(mnemonic, 64, "db");
        goto L0;
    } else if ((code & 0xc0) == 0xc0)
    {
        /* Scc */
        append_text(mnemonic, 64, "s");
L0:
        switch(code & 0xf00)
        {
        case 0x000:
            append_text(mnemonic, 64, "t");
            break;
        case 0x100:
            append_text(mnemonic, 64, "f");
            break;
        case 0x200:
            append_text(mnemonic, 64, "hi");
            break;
        case 0x300:
            append_text(mnemonic, 64, "ls");
            break;
        case 0x400:
            append_text(mnemonic, 64, "cc");
            break;
        case 0x500:
            append_text(mnemonic, 64, "cl");
            break;
        case 0x600:
            append_text(mnemonic, 64, "ne");
            break;
        case 0x700:
            append_text(mnemonic, 64, "eq");
            break;
        case 0x800:
            append_text(mnemonic, 64, "vc");
            break;
        case 0x900:
            append_text(mnemonic, 64, "vs");
            break;
        case 0xa00:
            append_text(mnemonic, 64, "pl");
            break;
        case 0xb00:
            append_text(mnemonic, 64, "mi");
            break;
        case 0xc00:
            append_text(mnemonic, 64, "ge");
            break;
        case 0xd00:
            append_text(mnemonic, 64, "lt");
            break;
        case 0xe00:
            append_text(mnemonic, 64, "gt");
            break;
        case 0xf00:
            append_text(mnemonic, 64, "le");
            break;
        }
        fill_space(mnemonic, 8);
        if ((code & 0xf8) != 0xc8)
            goto AddEA;  /* It must be Scc. */
        /* DBcc */
        offset = (signed short)((prog_ptr_u[addr + 2] << 8) + prog_ptr_u[addr + 3]);
        p = mnemonic + strlen(mnemonic);
        snprintf(p, remaining_text(mnemonic, 64, p), "d%1d,$%06x", code & 7, addr + 2 + offset);
        *next_addr = addr + 4;
        goto EndOfFunc;
    } else if (code & 0x100)
    {
        /* SUBQ */
        append_text(mnemonic, 64, "subq.");
        goto L1;
    } else
    {
        int v;
        /* ADDQ */
        append_text(mnemonic, 64, "addq.");
L1:
        p = mnemonic + strlen(mnemonic);
        switch(code & 0xc0)
        {
        case 0x00:
            size = 'b';
            break;
        case 0x40:
            size = 'w';
            break;
        case 0x80:
            size = 'l';
            break;
        }
        *(p++) = size; *(p++) = '\0';
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        v = (code & 0xe00) >> 9;
        snprintf(p, remaining_text(mnemonic, 64, p), "#%d,", v==0 ? 8:v);
    }
AddEA:
    p = mnemonic + strlen(mnemonic);
    b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
    if (b == FALSE)
        goto ErrorReturn;
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disa6(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    Long jaddr;
    char *p;

    switch(code & 0xf00)
    {
    case 0x000:
        append_text(mnemonic, 64, "bra");
        break;
    case 0x100:
        append_text(mnemonic, 64, "bsr");
        break;
    case 0x200:
        append_text(mnemonic, 64, "bhi");
        break;
    case 0x300:
        append_text(mnemonic, 64, "bls");
        break;
    case 0x400:
        append_text(mnemonic, 64, "bcc");
        break;
    case 0x500:
        append_text(mnemonic, 64, "bcs");
        break;
    case 0x600:
        append_text(mnemonic, 64, "bne");
        break;
    case 0x700:
        append_text(mnemonic, 64, "beq");
        break;
    case 0x800:
        append_text(mnemonic, 64, "bvc");
        break;
    case 0x900:
        append_text(mnemonic, 64, "bvs");
        break;
    case 0xa00:
        append_text(mnemonic, 64, "bpl");
        break;
    case 0xb00:
        append_text(mnemonic, 64, "bmi");
        break;
    case 0xc00:
        append_text(mnemonic, 64, "bge");
        break;
    case 0xd00:
        append_text(mnemonic, 64, "blt");
        break;
    case 0xe00:
        append_text(mnemonic, 64, "bgt");
        break;
    case 0xf00:
        append_text(mnemonic, 64, "ble");
        break;
    default:
        goto ErrorReturn;
    }
    if (prog_ptr[addr + 1] != 0)
    {
        jaddr = addr + 2 + prog_ptr[addr + 1];
        (*next_addr) = addr + 2;
        append_text(mnemonic, 64, ".b");
    } else
    {
        jaddr = addr + 2 +
            (short)(((unsigned short)prog_ptr[addr + 2] << 8) +
                     (unsigned short)prog_ptr[addr + 3]);
        (*next_addr) = addr + 4;
        append_text(mnemonic, 64, ".w");
    }
    fill_space(mnemonic, 8);
    p = mnemonic + strlen(mnemonic);
    snprintf(p, remaining_text(mnemonic, 64, p), "$%06X", jaddr);
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disa7(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    if (code & 0x0100)
    {
        *next_addr = addr;
        return NULL;
    } else
    {
        snprintf(mnemonic, 64, "moveq.l #%d,d%1d", (Long)((signed char)(code & 0xff)),
                (code & 0x0e00) >> 9);
    }
    *next_addr = addr + 2;
    return mnemonic;
}

static char *disa8(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    BOOL b;
    char *p, size = ' ';

    if ((code & 0x1f0) == 0x100)
    {
        /* SBCD */
        if (code & 0x8)
        {
            snprintf(mnemonic, 64, "sbcd    (a%1d)+,(a%1d)+", (code & 0x7), (code & 0x0e00) >> 9);
        } else
        {
            snprintf(mnemonic, 64, "sbcd    d%1d,d%1d", (code & 0x7), (code & 0x0e00) >> 9);
        }
        addr += 2;
        goto EndOfFunc;
    } else if ((code & 0x1c0) == 0x1c0)
    {
        /* DIVS */
        snprintf(mnemonic, 64, "divs    d%1d,", (code & 0x0e00) >> 9);
        size = 'w';
        goto L0;
    } else if ((code & 0x1c0) == 0x0c0)
    {
        /* DIVU */
        snprintf(mnemonic, 64, "divu    d%1d,", (code & 0x0e00) >> 9);
        size = 'w';
L0:
        fill_space(mnemonic, 8);
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), &addr);
        if (b == FALSE)
            goto ErrorReturn;
        goto EndOfFunc;
    } else
    {
        char size;
        char ea[64];
        /* OR */
        switch((code & 0x1c0) >> 6)
        {
        case 0:
            size = 'b';
            append_text(mnemonic, 64, "or.b");
            goto L1;
        case 1:
            size = 'w';
            append_text(mnemonic, 64, "or.w");
            goto L1;
        case 2:
            size = 'l';
            append_text(mnemonic, 64, "or.l");
L1:
            b = effective_address(addr, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, ea, sizeof(ea), &addr);
            if (b == FALSE)
                goto ErrorReturn;
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            snprintf(p, remaining_text(mnemonic, 64, p), "%s,d%1d", ea, (code & 0xe00) >> 9);
            break;
        case 4:
            size = 'b';
            append_text(mnemonic, 64, "or.b");
            goto L2;
        case 5:
            size = 'w';
            append_text(mnemonic, 64, "or.w");
            goto L2;
        case 6:
            size = 'l';
            append_text(mnemonic, 64, "or.l");
L2:
            b = effective_address(addr, (short)((code & 0x38) >> 3), (short)(code & 0x7), ' ', 0xfff, ea, sizeof(ea), &addr);
            if (b == FALSE)
                goto ErrorReturn;
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            snprintf(p, remaining_text(mnemonic, 64, p), "d%1d,%s", (code & 0xe00) >> 9, ea);
            break;
        default:
            goto ErrorReturn;
        }
    }
EndOfFunc:
    *next_addr = addr;
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disa9_d(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    BOOL b;
    char *p, size, reg = 'd';

    if ((code & 0xf130) == 0x9100 && ((code & 0xc0) >> 6) <= 2)
    {
        /* SUBX */
        append_text(mnemonic, 64, "subx");
        goto L0;
    } else if ((code & 0xf130) == 0xd100 && ((code & 0xc0) >> 6) <= 2)
    {
        /* ADDX */
        append_text(mnemonic, 64, "addx");
L0:
        switch((code & 0xc0) >> 6)
        {
        case 0:
            size = 'b';
            break;
        case 1:
            size = 'w';
            break;
        case 2:
            size = 'l';
            break;
        default:
            goto ErrorReturn;
        }
        p = mnemonic + strlen(mnemonic);
        if (code & 0x8)
        {
            snprintf(p, remaining_text(mnemonic, 64, p), ".%c  -(a%1d),-(a%1d)", size, (code & 0x7), (code & 0xe00) >> 9);
        } else
        {
            snprintf(p, remaining_text(mnemonic, 64, p), ".%c  d%1d,d%1d", size, (code & 0x7), (code & 0xe00) >> 9);
        }
        goto EndOfFunc;
    } else if ((code & 0xf000) == 0x9000)
    {
        /* SUB or SUBA*/
        append_text(mnemonic, 64, "sub");
        goto L1;
    } else
    {
        /* ADD or ADDA */
        append_text(mnemonic, 64, "add");
L1:
        /* ADD & SUB共通処理 */
        switch((code & 0x1c0) >> 6)
        {
        case 0:
            append_text(mnemonic, 64, ".b");
            size = 'b';
            goto L2;
        case 3:
            append_text(mnemonic, 64, "a");
            reg = 'a';
        case 1:
            append_text(mnemonic, 64, ".w");
            size = 'w';
            goto L2;
        case 7:
            append_text(mnemonic, 64, "a");
            reg = 'a';
        case 2:
            append_text(mnemonic, 64, ".l");
            size = 'l';
L2:
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
            if (b == FALSE)
                goto ErrorReturn;
            p = mnemonic + strlen(mnemonic);
            snprintf(p, remaining_text(mnemonic, 64, p), ",%c%1d", reg, (code & 0xe00) >> 9);
            break;
        case 4:
            append_text(mnemonic, 64, ".b");
            goto L3;
        case 5:
            append_text(mnemonic, 64, ".w");
            goto L3;
        case 6:
            append_text(mnemonic, 64, ".l");
L3:
            fill_space(mnemonic, 8);
            p = mnemonic + strlen(mnemonic);
            snprintf(p, remaining_text(mnemonic, 64, p), "d%1d,", (code & 0xe00) >> 9);
            p = mnemonic + strlen(mnemonic);
            b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), ' ', 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
            if (b == FALSE)
                goto ErrorReturn;
            break;
        default:
            goto ErrorReturn;
        }
    }
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disab(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char size, reg = 'd';
    char *p;
    BOOL b;

    if ((code & 0xf138) == 0xb108 && ((code & 0xc0) >> 6) <= 2)
    {
        /* CMPM */
        append_text(mnemonic, 64, "cmpm");
        switch((code & 0xc0) >> 6)
        {
        case 0:
            size = 'b';
            break;
        case 1:
            size = 'w';
            break;
        case 2:
            size = 'l';
            break;
        default:
            goto ErrorReturn;
        }
        snprintf(mnemonic, 64, "cmpm.%c  (a%1d)+,(a%1d)+", size, (code & 0x3), (code & 0xe00) >> 9);
        *next_addr = addr + 2;
        goto EndOfFunc;
    }
    /* CMP or EOR*/
    switch((code & 0x1c0) >> 6)
    {
    case 0:
    case 4:
        size = 'b';
        break;
    case 3:
        reg = 'a';
    case 1:
    case 5:
        size = 'w';
        break;
    case 7:
        reg = 'a';
    case 2:
    case 6:
        size = 'l';
        break;
    default:
        goto ErrorReturn;
    }
    if ((code & 0xf100) == 0xb100 && reg != 'a')
    {
        /* EOR */
        snprintf(mnemonic, 64, "eor.%c   d%1d,", size, (code & 0xe00) >> 9);
    } else
    {
        /* CMP */
        if (reg == 'd')
        {
            snprintf(mnemonic, 64, "cmp.%c   ", size);
        } else
        {
            snprintf(mnemonic, 64, "cmpa.%c  ", size);
        }
    }
    p = mnemonic + strlen(mnemonic);
    b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
    if (b == FALSE)
        goto ErrorReturn;
    if ((code & 0xf100) == 0xb100 && reg != 'a')
    {
        /* EOR */
        goto EndOfFunc;
    }
    p = mnemonic + strlen(mnemonic);
    snprintf(p, remaining_text(mnemonic, 64, p), ",%c%1d", reg, (code & 0xe00) >> 9);
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disac(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char size;
    char *p;
    BOOL b;

    /* まず、10ビット固定の命令を処理する */
    switch(code & 0xf1f8)
    {
    case 0xc100:
        snprintf(mnemonic, 64, "abcd.b  d%1d,d%1d", (code & 0x7), (code & 0x38) >> 9);
        *next_addr = addr + 2;
        goto EndOfFunc;
    case 0xc108:
        snprintf(mnemonic, 64, "abcd.b  -(a%1d),-(a%1d)", (code & 0x7), (code & 0x38) >> 9);
        *next_addr = addr + 2;
        goto EndOfFunc;
    case 0xc140:
        snprintf(mnemonic, 64, "exg.l  d%1d,d%1d", (code & 0x38) >> 9, (code & 0x7));
        *next_addr = addr + 2;
        goto EndOfFunc;
    case 0xc141:
        snprintf(mnemonic, 64, "exg.l  a%1d,a%1d", (code & 0x38) >> 9, (code & 0x7));
        *next_addr = addr + 2;
        goto EndOfFunc;
    case 0xc181:
        snprintf(mnemonic, 64, "exg.l  d%1d,a%1d", (code & 0x38) >> 9, (code & 0x7));
        *next_addr = addr + 2;
        goto EndOfFunc;
    }
    /* 残りの3命令を処理する */
    switch(code & 0xf1c0)
    {
    case 0xc0c0:
        /* MULU */
        append_text(mnemonic, 64, "mulu    ");
        size = 'w';
        goto L2;
    case 0xc1c0:
        /* MULS */
        append_text(mnemonic, 64, "muls    ");
        size = 'w';
        goto L2;
    default:
        /* AND */
        switch((code & 0x1c0) >> 6)
        {
        case 0:
            size = 'b';
            goto L0;
        case 1:
            size = 'w';
            goto L0;
        case 2:
            size = 'l';
L0:
            snprintf(mnemonic, 64, "and.%c   ", size);
L2:
            p = mnemonic + strlen(mnemonic);
            b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
            if (b == FALSE)
                goto ErrorReturn;
            p = mnemonic + strlen(mnemonic);
            snprintf(p, remaining_text(mnemonic, 64, p), ",d%1d", (code & 0xe00) >> 9);
            goto EndOfFunc;
        case 4:
            size = 'b';
            goto L1;
        case 5:
            size = 'w';
            goto L1;
        case 6:
            size = 'l';
L1:
            snprintf(mnemonic, 64, "and.%c   d%1d,", size, (code & 0xe00) >> 9);
            p = mnemonic + strlen(mnemonic);
            b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), size, 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
            if (b == FALSE)
                goto ErrorReturn;
            goto EndOfFunc;
        }
    }
EndOfFunc:
    return mnemonic;
ErrorReturn:
    return NULL;
}

static char *disae(Long addr, unsigned short code, Long *next_addr, char *mnemonic)
{
    char size, dir, count[10];
    char *p;
    BOOL b;

    if (code & 0x0100)
    {
        dir = 'l';
    } else
    {
        dir = 'r';
    }
    if ((code & 0xf0c0) == 0xe0c0)
    {
        switch((code & 0xc0) >> 6)
        {
        case 0:
            size = 'b';
            break;
        case 1:
            size = 'w';
            break;
        case 2:
            size = 'l';
            break;
        default:
            goto ErrorReturn;
        }

        switch((code & 0x0600) >> 9)
        {
        case 0:
            snprintf(mnemonic, 64, "as%c.%c   ", dir, size);
            break;
        case 1:
            snprintf(mnemonic, 64, "ls%c.%c   ", dir, size);
            break;
        case 2:
            snprintf(mnemonic, 64, "ro%cx.%c  ", dir, size);
            break;
        case 3:
            snprintf(mnemonic, 64, "ro%c.%c   ", dir, size);
            break;
        }
        p = mnemonic + strlen(mnemonic);
        b = effective_address(addr + 2, (short)((code & 0x38) >> 3), (short)(code & 0x7), ' ', 0xfff, p, remaining_text(mnemonic, 64, p), next_addr);
        if (b == FALSE)
            goto ErrorReturn;
    } else
    {
        switch((code & 0xc0) >> 6)
        {
        case 0:
            size = 'b';
            break;
        case 1:
            size = 'w';
            break;
        case 2:
            size = 'l';
            break;
        default:
            goto ErrorReturn;
        }

        if (code & 0x20)
        {
            int iw = (code & 0x0e00) >> 9;

            snprintf(count, sizeof(count), "d%1d", iw);
        } else
        {
            int iw = (code & 0x0e00) >> 9;

            iw = (iw == 0) ? 8 : iw;
            snprintf(count, sizeof(count), "#%1d", iw);
        }
        switch((code & 0x0018) >> 3)
        {
        case 0:
            snprintf(mnemonic, 64, "as%c.%c   %s,d%1d", dir, size, count, code & 0x7);
            break;
        case 1:
            snprintf(mnemonic, 64, "ls%c.%c   %s,d%1d", dir, size, count, code & 0x7);
            break;
        case 2:
            snprintf(mnemonic, 64, "ro%cx.%c  %s,d%1d", dir, size, count, code & 0x7);
            break;
        case 3:
            snprintf(mnemonic, 64, "ro%c.%c   %s,d%1d", dir, size, count, code & 0x7);
            break;
        }
        *next_addr = addr + 2;
    }
    return mnemonic;
ErrorReturn:
    return NULL;
}

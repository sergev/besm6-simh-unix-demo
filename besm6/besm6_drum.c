/*
 * besm6_drum.c: BESM-6 magnetic drum device
 *
 * Copyright (c) 2009, Serge Vakulenko
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.

 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */
#include "besm6_defs.h"

/*
 * Управляющее слово обмена с магнитным барабаном.
 */
#define DRUM_READ_OVERLAY 020000000 /* считывание с наложением */
#define DRUM_PARITY_FLAG                                                        \
    010000000                        /* блокировка считывания слов с неверной \
                                      * чётностью или запись с неверной чётностью */
#define DRUM_READ_SYSDATA 004000000  /* считывание только служебных слов */
#define DRUM_PAGE_MODE    001000000  /* обмен целой страницей */
#define DRUM_READ         000400000  /* чтение с барабана в память */
#define DRUM_PAGE         000370000  /* номер страницы памяти */
#define DRUM_BLOCK        0740000000 /* номер блока памяти - 27-24 рр */
#define DRUM_PARAGRAF     000006000  /* номер абзаца */
#define DRUM_UNIT         000001600  /* номер барабана */
#define DRUM_CYLINDER     000000174  /* номер тракта на барабане */
#define DRUM_SECTOR       000000003  /* номер сектора */

/*
 * Параметры обмена с внешним устройством.
 */
int drum_op;     /* Условное слово обмена */
int drum_zone;   /* Номер зоны на барабане */
int drum_sector; /* Начальный номер сектора на барабане */
int drum_memory; /* Начальный адрес памяти */
int drum_nwords; /* Количество слов обмена */
int drum_fail;   /* Маска ошибок по направлениям */

t_stat drum_event(UNIT *u);

/*
 * DRUM data structures
 *
 * drum_dev     DRUM device descriptor
 * drum_unit    DRUM unit descriptor
 * drum_reg     DRUM register list
 */
UNIT drum_unit[] = {
    { .action = drum_event, .flags = UNIT_ATTABLE | UNIT_ROABLE },
    { .action = drum_event, .flags = UNIT_ATTABLE | UNIT_ROABLE },
};

t_stat drum_reset(DEVICE *dptr);
t_stat drum_attach(UNIT *uptr, const char *cptr);
t_stat drum_detach(UNIT *uptr);

DEVICE drum_dev = { .name = "DRUM", .units = drum_unit, .numunits = 2, .reset = &drum_reset, .detach = &drum_detach };

/*
 * Reset routine
 */
t_stat drum_reset(DEVICE *dptr)
{
    drum_op     = 0;
    drum_zone   = 0;
    drum_sector = 0;
    drum_memory = 0;
    drum_nwords = 0;
    sim_cancel(&drum_unit[0]);
    sim_cancel(&drum_unit[1]);
    return SCPE_OK;
}

t_stat drum_attach(UNIT *u, const char *cptr)
{
    t_stat s;

    s = attach_unit(u, cptr);
    if (s != SCPE_OK)
        return s;
    if (u == &drum_unit[0])
        GRP |= GRP_DRUM1_FREE;
    else
        GRP |= GRP_DRUM2_FREE;
    return SCPE_OK;
}

t_stat drum_detach(UNIT *u)
{
    if (u == &drum_unit[0])
        GRP &= ~GRP_DRUM1_FREE;
    else
        GRP &= ~GRP_DRUM2_FREE;
    return detach_unit(u);
}

/*
 * Запись на барабан.
 */
void drum_write(UNIT *u)
{
    int ctlr;
    t_value *sysdata;

    ctlr    = (u == &drum_unit[1]);
    sysdata = ctlr ? &memory[020] : &memory[010];
    if (fseek(u->fileref, ZONE_SIZE * drum_zone * 8, SEEK_SET) == 0) {
        fwrite(sysdata, 8, 8, u->fileref);
        fwrite(&memory[drum_memory], 8, 1024, u->fileref);
    }
    if (ferror(u->fileref))
        longjmp(cpu_halt, SCPE_IOERR);
}

void drum_write_sector(UNIT *u)
{
    int ctlr;
    t_value *sysdata;

    ctlr    = (u == &drum_unit[1]);
    sysdata = ctlr ? &memory[020] : &memory[010];
    if (fseek(u->fileref, (ZONE_SIZE * drum_zone + drum_sector * 2) * 8, SEEK_SET) == 0) {
        fwrite(&sysdata[drum_sector * 2], 8, 2, u->fileref);
        if (fseek(u->fileref, (ZONE_SIZE * drum_zone + 8 + drum_sector * 256) * 8, SEEK_SET) == 0) {
            fwrite(&memory[drum_memory], 8, 256, u->fileref);
        }
    }
    if (ferror(u->fileref))
        longjmp(cpu_halt, SCPE_IOERR);
}

/*
 * Чтение с барабана.
 */
void drum_read(UNIT *u)
{
    int ctlr;
    t_value *sysdata;

    ctlr    = (u == &drum_unit[1]);
    sysdata = ctlr ? &memory[020] : &memory[010];
    if (fseek(u->fileref, ZONE_SIZE * drum_zone * 8, SEEK_SET) != 0 ||
        fread(sysdata, 8, 8, u->fileref) != 8) {
        /* Чтение неинициализированного барабана */
        drum_fail |= 0100 >> ctlr;
        return;
    }
    if (!(drum_op & DRUM_READ_SYSDATA) &&
        fread(&memory[drum_memory], 8, 1024, u->fileref) != 1024) {
        /* Чтение неинициализированного барабана */
        drum_fail |= 0100 >> ctlr;
        return;
    }
    if (ferror(u->fileref))
        longjmp(cpu_halt, SCPE_IOERR);
}

void drum_read_sector(UNIT *u)
{
    int ctlr;
    t_value *sysdata;

    ctlr    = (u == &drum_unit[1]);
    sysdata = ctlr ? &memory[020] : &memory[010];
    if (fseek(u->fileref, (ZONE_SIZE * drum_zone + drum_sector * 2) * 8, SEEK_SET) != 0 ||
        fread(&sysdata[drum_sector * 2], 8, 2, u->fileref) != 2) {
        /* Чтение неинициализированного барабана */
        drum_fail |= 0100 >> ctlr;
        return;
    }
    if (!(drum_op & DRUM_READ_SYSDATA)) {
        if (fseek(u->fileref, (ZONE_SIZE * drum_zone + 8 + drum_sector * 256) * 8, SEEK_SET) != 0 ||
            fread(&memory[drum_memory], 8, 256, u->fileref) != 256) {
            /* Чтение неинициализированного барабана */
            drum_fail |= 0100 >> ctlr;
            return;
        }
    }
    if (ferror(u->fileref))
        longjmp(cpu_halt, SCPE_IOERR);
}

static void clear_memory(t_value *p, int nwords)
{
    while (nwords-- > 0)
        *p++ = SET_PARITY(0, PARITY_NUMBER);
}

/*
 * Выполнение обращения к барабану.
 */
void drum(int ctlr, uint32 cmd)
{
    UNIT *u = &drum_unit[ctlr];

    drum_op = cmd;
    if (drum_op & DRUM_PAGE_MODE) {
        /* Обмен страницей */
        drum_nwords = 1024;
        drum_zone   = (cmd & (DRUM_UNIT | DRUM_CYLINDER)) >> 2;
        drum_sector = 0;
        drum_memory = (cmd & DRUM_PAGE) >> 2 | (cmd & DRUM_BLOCK) >> 8;
        if (drum_dev.dctrl)
            besm6_debug("### %s drum %c%d zone %02o mem %05o-%05o",
                        (drum_op & DRUM_READ) ? "read" : "write", ctlr + '1', (drum_zone >> 5 & 7),
                        drum_zone & 037, drum_memory, drum_memory + drum_nwords - 1);
        if (drum_op & DRUM_READ) {
            clear_memory(ctlr ? &memory[020] : &memory[010], 8);
            if (!(drum_op & DRUM_READ_SYSDATA))
                clear_memory(&memory[drum_memory], 1024);
        }
    } else {
        /* Обмен сектором */
        drum_nwords = 256;
        drum_zone   = (cmd & (DRUM_UNIT | DRUM_CYLINDER)) >> 2;
        drum_sector = cmd & DRUM_SECTOR;
        drum_memory = (cmd & (DRUM_PAGE | DRUM_PARAGRAF)) >> 2 | (cmd & DRUM_BLOCK) >> 8;
        if (drum_dev.dctrl)
            besm6_debug("### %s drum %c%d zone %02o sector %d mem %05o-%05o",
                        (drum_op & DRUM_READ) ? "read" : "write", ctlr + '1', (drum_zone >> 5 & 7),
                        drum_zone & 037, drum_sector & 3, drum_memory,
                        drum_memory + drum_nwords - 1);
        if (drum_op & DRUM_READ) {
            clear_memory(ctlr ? &memory[020 + drum_sector * 2] : &memory[010 + drum_sector * 2], 2);
            if (!(drum_op & DRUM_READ_SYSDATA))
                clear_memory(&memory[drum_memory], 256);
        }
    }
    if (!u->fileref) {
        /* Device not attached. */
        drum_fail |= 0100 >> ctlr;
        return;
    }
    drum_fail &= ~(0100 >> ctlr);
    if (drum_op & DRUM_READ_OVERLAY) {
        /* Not implemented. */
        longjmp(cpu_halt, SCPE_NOFNC);
    }
    if (drum_op & DRUM_READ) {
        if (drum_op & DRUM_PAGE_MODE)
            drum_read(u);
        else
            drum_read_sector(u);
    } else {
        if (drum_op & DRUM_PARITY_FLAG) {
            besm6_log("### drum write with bad parity not implemented");
            longjmp(cpu_halt, SCPE_NOFNC);
        }
        if (u->flags & UNIT_RO) {
            /* Read only. */
            longjmp(cpu_halt, SCPE_RO);
        }
        if (drum_op & DRUM_PAGE_MODE)
            drum_write(u);
        else
            drum_write_sector(u);
    }

    /* Гасим главный регистр прерываний. */
    if (u == &drum_unit[0])
        GRP &= ~GRP_DRUM1_FREE;
    else
        GRP &= ~GRP_DRUM2_FREE;

    /* Ждём события от устройства.
     * Согласно данным из книжки Мазного Г.Л.,
     * даём 20 мсек на обмен, или 200 тыс.тактов. */
    /*sim_activate (u, 20*MSEC);*/
    sim_activate(u, 20 * USEC); /* Ускорим для отладки. */
}

/*
 * Событие: закончен обмен с МБ.
 * Устанавливаем флаг прерывания.
 */
t_stat drum_event(UNIT *u)
{
    if (u == &drum_unit[0])
        GRP |= GRP_DRUM1_FREE;
    else
        GRP |= GRP_DRUM2_FREE;
    return SCPE_OK;
}

/*
 * Опрос ошибок обмена командой 033 4035.
 */
int drum_errors()
{
    return drum_fail;
}

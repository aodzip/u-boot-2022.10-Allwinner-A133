#include <common.h>
#include <asm/arch/dram.h>
#include <asm/arch/cpu.h>

#include "ida_defs.h"

#ifndef _REG
#define _REG(x) (*((volatile unsigned int *)(x)))
#endif

static unsigned int auto_cal_timing(int a1, int a2)
{
  unsigned int v2; // r4
  unsigned int v3; // r1
  unsigned int result; // r0

  v2 = a2 * a1;
  v3 = a2 * a1 % 1000u;
  result = v2 / 1000;
  if ( v3 )
    ++result;
  return result;
}

void mctl_set_timing_params(struct dram_para *para)
{
  int v1; // r7
  unsigned int v3; // r5
  unsigned int v4; // r8
  unsigned int v5; // r9
  unsigned int v6; // r6
  _DWORD *v7; // r10
  int v9; // r4
  int v10; // r0
  int v11; // r3
  int v12; // r1
  int v13; // r0
  int v14; // r0
  int v15; // r0
  int v16; // r3
  int v17; // r0
  int v18; // r3
  int v19; // r0
  int v20; // r3
  int v21; // r0
  int v22; // r3
  int v23; // r0
  int v24; // r0
  int v25; // r3
  int v26; // r0
  int v27; // r1
  int v29; // r0
  int v30; // r0
  int v31; // r0
  char v32; // r5
  char v33; // r0
  int v34; // r0
  int v36; // r0
  uint32_t dram_type; // [sp+0h] [bp-B0h]
  int v38; // [sp+4h] [bp-ACh]
  int v39; // [sp+8h] [bp-A8h]
  int v40; // [sp+Ch] [bp-A4h]
  unsigned int v41; // [sp+10h] [bp-A0h]
  int v42; // [sp+14h] [bp-9Ch]
  int v43; // [sp+18h] [bp-98h]
  int v44; // [sp+1Ch] [bp-94h]
  int v45; // [sp+20h] [bp-90h]
  int v46; // [sp+24h] [bp-8Ch]
  int v47; // [sp+28h] [bp-88h]
  int v48; // [sp+2Ch] [bp-84h]
  int v49; // [sp+30h] [bp-80h]
  char v50; // [sp+34h] [bp-7Ch]
  int v51; // [sp+38h] [bp-78h]
  int v52; // [sp+3Ch] [bp-74h]
  int v53; // [sp+40h] [bp-70h]
  int v54; // [sp+44h] [bp-6Ch]
  int v55; // [sp+48h] [bp-68h]
  int v56; // [sp+4Ch] [bp-64h]
  int v57; // [sp+50h] [bp-60h]
  int v58; // [sp+54h] [bp-5Ch]
  int v59; // [sp+58h] [bp-58h]
  int v60; // [sp+5Ch] [bp-54h]
  int v61; // [sp+60h] [bp-50h]
  int v62; // [sp+64h] [bp-4Ch]
  int v63; // [sp+68h] [bp-48h]
  unsigned int v64; // [sp+6Ch] [bp-44h]
  int v66; // [sp+74h] [bp-3Ch]
  int v67; // [sp+78h] [bp-38h]
  int v68; // [sp+7Ch] [bp-34h]
  int v69; // [sp+80h] [bp-30h]

  v1 = 3;
  dram_type = para->type;
  v3 = 3;
  LOBYTE(v4) = 3;
  v5 = 6;
  v55 = 3;
  v47 = 6;
  v38 = 3;
  v46 = 6;
  v64 = 24 * (unsigned __int8)(_REG(0x3001011) + 1);
  v42 = 4;
  v67 = 4;
  v68 = 4;
  v66 = 8;
  v69 = 8;
  v50 = 1;
  v63 = 2;
  v39 = 4;
  v41 = 4;
  v49 = 4;
  v61 = 27;
  v62 = 8;
  v54 = 12;
  v60 = 128;
  v53 = 98;
  v48 = 10;
  v58 = 16;
  v59 = 14;
  v57 = 20;
  v6 = 2;
  v45 = 0;
  v56 = 2;
  v52 = 2;
  v40 = 3;
  v51 = 3;
  v43 = 1;
  v44 = 1;

  u32 dram_mr0 = 0x0;
  u32 dram_mr1 = 0xc3;
  u32 dram_mr2 = 0x6;
  u32 dram_mr3 = 0x2;
  u32 dram_mr4 = 0x0;
  u32 dram_mr5 = 0x0;
  u32 dram_mr6 = 0x0;
  u32 dram_mr11 = 0x0;
  u32 dram_mr12  = 0x0;
  u32 dram_mr14 = 0x0;
  u32 dram_mr22 = 0x0;
  u32 dram_tpr2 = 0x0;
  u32 dram_tpr13 = 0x60;

  while ( v1 != -1 )
  {
    dram_tpr13 = dram_tpr13;
    if ( (dram_tpr13 & 0x805) != 5 )
      v1 = 0;
    v7 = (_DWORD *)(dram_tpr13 & 4);
    if ( (dram_tpr13 & 4) != 0 )
    {
      dram_tpr2 = dram_tpr2;
      switch ( v1 )
      {
        case 0:
          v7 = 0;
          v9 = v64 / ((dram_tpr2 & 0x1Fu) + 1);
          goto LABEL_8;
        case 1:
          v12 = (dram_tpr2 >> 8) & 0x1F;
          break;
        case 2:
          v12 = HIWORD(dram_tpr2) & 0x1F;
          break;
        default:
          v12 = HIBYTE(dram_tpr2) & 0x1F;
          break;
      }
      v9 = v64 / (v12 + 1);
LABEL_18:
      v7 = (_DWORD *)((v1 + 1) << 12);
      goto LABEL_8;
    }
    v9 = v64 >> 2;
    if ( v1 )
      goto LABEL_18;
LABEL_8:
    switch ( dram_type )
    {
      case 3u:
        v58 = (unsigned __int8)auto_cal_timing(50, v9);
        v10 = (unsigned __int8)auto_cal_timing(10, v9);
        if ( (unsigned __int8)v10 < 2u )
          v10 = 2;
        v38 = v10;
        v47 = (unsigned __int8)auto_cal_timing(15, v9);
        v57 = (unsigned __int8)auto_cal_timing(53, v9);
        v3 = (unsigned __int8)auto_cal_timing(8, v9);
        if ( v3 < 2 )
          v3 = 2;
        v59 = (unsigned __int8)auto_cal_timing(38, v9);
        LOBYTE(v4) = v3;
        v53 = (unsigned __int16)(auto_cal_timing(7800, v9) >> 5);
        v60 = (unsigned __int16)auto_cal_timing(350, v9);
        v5 = v47;
        v68 = (unsigned __int8)(auto_cal_timing(360, v9) >> 5);
        v48 = v3;
        goto LABEL_14;
      case 4u:
        v58 = (unsigned __int8)auto_cal_timing(35, v9);
        v13 = (unsigned __int8)auto_cal_timing(8, v9);
        if ( (unsigned __int8)v13 < 2u )
          v13 = 2;
        v38 = v13;
        v14 = (unsigned __int8)auto_cal_timing(6, v9);
        if ( (unsigned __int8)v14 < 2u )
          v14 = 2;
        v48 = v14;
        v15 = (unsigned __int8)auto_cal_timing(10, v9);
        if ( (unsigned __int8)v15 < 8u )
          v15 = 8;
        v66 = v15;
        v47 = (unsigned __int8)auto_cal_timing(15, v9);
        v57 = (unsigned __int8)auto_cal_timing(49, v9);
        v16 = (unsigned __int8)auto_cal_timing(3, v9);
        if ( !v16 )
          LOBYTE(v16) = 1;
        v50 = v16;
        v59 = (unsigned __int8)auto_cal_timing(34, v9);
        v53 = (unsigned __int16)(auto_cal_timing(7800, v9) >> 5);
        v60 = (unsigned __int16)auto_cal_timing(350, v9);
        LOBYTE(v4) = v38;
        v68 = (unsigned __int8)(auto_cal_timing(360, v9) >> 5);
        v5 = v47;
        v63 = v48;
        v11 = 3;
        goto LABEL_34;
      case 7u:
        v17 = (unsigned __int8)auto_cal_timing(50, v9);
        if ( (unsigned __int8)v17 < 4u )
          v17 = 4;
        v58 = v17;
        v18 = (unsigned __int8)auto_cal_timing(10, v9);
        if ( !v18 )
          v18 = 1;
        v38 = v18;
        v19 = (unsigned __int8)auto_cal_timing(24, v9);
        if ( (unsigned __int8)v19 < 2u )
          v19 = 2;
        v47 = v19;
        v57 = (unsigned __int8)auto_cal_timing(70, v9);
        v3 = (unsigned __int8)auto_cal_timing(8, v9);
        if ( v3 < 2 )
          v3 = 2;
        v5 = (unsigned __int8)auto_cal_timing(27, v9);
        v59 = (unsigned __int8)auto_cal_timing(42, v9);
        LOBYTE(v4) = v3;
        v53 = (unsigned __int16)(auto_cal_timing(3900, v9) >> 5);
        v60 = (unsigned __int16)auto_cal_timing(210, v9);
        v48 = v3;
        v67 = (unsigned __int8)auto_cal_timing(220, v9);
LABEL_14:
        v11 = 2;
LABEL_34:
        v56 = v11;
        break;
      case 8u:
        v58 = (unsigned __int8)auto_cal_timing(40, v9);
        v4 = (unsigned __int8)auto_cal_timing(10, v9);
        v20 = v4;
        if ( v4 < 2 )
          v20 = 2;
        v38 = v20;
        v21 = (unsigned __int8)auto_cal_timing(18, v9);
        if ( (unsigned __int8)v21 < 2u )
          v21 = 2;
        v47 = v21;
        v57 = (unsigned __int8)auto_cal_timing(65, v9);
        v3 = (unsigned __int8)auto_cal_timing(8, v9);
        v22 = v3;
        if ( v3 < 2 )
          v22 = 2;
        v48 = v22;
        if ( (dram_tpr13 & 0x10000000) != 0 )
          v4 = (unsigned __int8)auto_cal_timing(12, v9);
        if ( v4 < 4 )
          LOBYTE(v4) = 4;
        if ( v3 < 4 )
          v3 = 4;
        v5 = (unsigned __int8)auto_cal_timing(21, v9);
        v59 = (unsigned __int8)auto_cal_timing(42, v9);
        v53 = (unsigned __int16)(auto_cal_timing(3904, v9) >> 5);
        v60 = (unsigned __int16)auto_cal_timing(280, v9);
        v67 = (unsigned __int8)auto_cal_timing(290, v9);
        v11 = 4;
        goto LABEL_34;
    }
    switch ( dram_type )
    {
      case 3u:
        LOBYTE(v6) = auto_cal_timing(8, v9);
        v41 = (unsigned __int8)auto_cal_timing(10, v9);
        if ( v41 <= 2 )
        {
          v6 = 6;
        }
        else
        {
          v6 = (unsigned __int8)v6;
          if ( (unsigned __int8)v6 < 2u )
            v6 = 2;
        }
        v55 = (unsigned __int8)(v6 + 1);
        dram_mr2 = dram_mr2;
        v61 = (unsigned __int8)(v9 / 15);
        dram_mr0 = 0x1F14;
        dram_mr2 = dram_mr2 & 0xFFFFFFC7 | 0x20;
        dram_mr3 = 0;
        if ( (int)(v3 + v5) <= 8 )
          v3 = (unsigned __int8)(9 - v5);
        v62 = (unsigned __int8)(v4 + 7);
        v39 = v41;
        v49 = 5;
        v54 = 14;
        v46 = 12;
        goto LABEL_72;
      case 4u:
        v23 = (unsigned __int8)auto_cal_timing(15, v9);
        if ( (unsigned __int8)v23 < 0xCu )
          v23 = 12;
        v46 = v23;
        v6 = (unsigned __int8)auto_cal_timing(5, v9);
        if ( v6 < 2 )
          v6 = 2;
        v24 = (unsigned __int8)auto_cal_timing(10, v9);
        if ( (unsigned __int8)v24 < 3u )
          v24 = 3;
        v55 = (unsigned __int8)(v6 + 1);
        v41 = v24;
        v42 = (unsigned __int8)(auto_cal_timing(170, v9) >> 5);
        v61 = (unsigned __int8)(auto_cal_timing(70200, v9) >> 10);
        if ( v5 > 4 )
          v3 = 4;
        else
          v3 = 9 - v5;
        if ( v5 <= 4 )
          v3 = (unsigned __int8)v3;
        dram_mr2 = dram_mr2 & 0xFFFFFFC7 | 8;
        dram_mr0 = 1312;
        v62 = (unsigned __int8)(v4 + 7);
        v69 = (unsigned __int8)(v50 + 7);
        v39 = v41;
        v49 = 5;
        v54 = 14;
LABEL_72:
        v52 = 4;
        v45 = 0;
        v40 = 5;
        v51 = 7;
        v44 = 6;
        v25 = 10;
        goto LABEL_73;
      case 7u:
        dram_mr1 = 131;
        dram_mr2 = 28;
        dram_mr0 = dram_mr0;
        dram_mr0 = 0;
        v6 = 3;
        v62 = (unsigned __int8)(v4 + 9);
        v39 = 5;
        v41 = 5;
        v55 = 5;
        v49 = 13;
        v61 = 24;
        v54 = 16;
        v46 = 12;
        v52 = 5;
        v45 = 5;
        v40 = 4;
        v51 = 7;
        v44 = 6;
        v25 = 12;
        goto LABEL_73;
      case 8u:
        v29 = (unsigned __int8)auto_cal_timing(14, v9);
        if ( (unsigned __int8)v29 < 5u )
          v29 = 5;
        v45 = v29;
        v6 = (unsigned __int8)auto_cal_timing(15, v9);
        if ( v6 < 2 )
          v6 = 2;
        v30 = (unsigned __int8)auto_cal_timing(2, v9);
        if ( (unsigned __int8)v30 < 2u )
          v30 = 2;
        v41 = v30;
        v31 = (unsigned __int8)auto_cal_timing(5, v9);
        if ( (unsigned __int8)v31 < 2u )
          v31 = 2;
        v39 = v31;
        v61 = (unsigned __int8)((unsigned int)(9 * v53) >> 5);
        v32 = auto_cal_timing(4, v9) + 17;
        v33 = auto_cal_timing(1, v9);
        dram_mr1 = 52;
        dram_mr2 = 27;
        v49 = (unsigned __int8)(v32 - v33);
        v55 = v6;
        v3 = 4;
        v62 = (unsigned __int8)(v4 + 14);
        v52 = v45;
        v54 = 24;
        v46 = 12;
        v40 = 5;
        if ( (dram_tpr13 & 0x10000000) != 0 )
        {
          v51 = 11;
          v44 = 5;
          v25 = 19;
        }
        else
        {
          v51 = 10;
          v44 = 5;
          v25 = 17;
        }
LABEL_73:
        v43 = v25;
        break;
      default:
        break;
    }
    v7[0x1208040] = v59 | (v58 << 16) | (v54 << 24) | (v61 << 8);
    v7[0x1208041] = v57 | (v48 << 16) | (v3 << 8);
    v7[0x1208042] = (v51 << 16) | (v40 << 24) | v62 | (v49 << 8);
    v7[0x1208043] = (v52 << 12) | (v45 << 20) | v46;
    v7[0x1208044] = (v56 << 16) | (v47 << 24) | v5 | (v38 << 8);
    v7[0x1208045] = (v39 << 16) | (v41 << 24) | v6 | (v55 << 8);
    v7[0x1208046] = (v48 + 2) | 0x2020000;
    v7[0x1208048] = v68 | 0x1000 | (v42 << 24) | (v42 << 16);
    v7[0x1208049] = v69 | (v63 << 8) | 0x20000;
    v7[0x120804A] = 0xE0C05;
    v7[0x120804B] = 0x440C021C;
    v7[0x120804C] = v66;
    v7[0x120804D] = 0xA100002;
    v7[0x120804E] = v67;
    if ( dram_type == 7 )
    {
      v26 = _REG(0x48200D0) & 0x3C00FFFF | 0x4F0000;
LABEL_76:
      v27 = v26 & 0x3FFFF000 | 0x112;
      goto LABEL_98;
    }
    if ( dram_type != 8 )
    {
      v26 = _REG(0x48200D0) & 0x3FFFFFFF;
      goto LABEL_76;
    }
    v27 = _REG(0x48200D0) & 0x3FFFF000 | 0x3F0;
LABEL_98:
    _REG(0x48200D0) = v27;
    if ( (dram_tpr13 & 8) != 0 )
      v34 = 0x420000;
    else
      v34 = 0x1F20000;
    _REG(0x48200D4) = v34;
    _REG(0x48200D8) = _REG(0x48200D8) & 0xFFFF00F0 | 0xD05;
    _REG(0x48201B0) = 0;
    dram_mr1 = dram_mr1;
    if ( dram_type - 6 > 2 )
    {
      v7[0x1208037] = dram_mr1 | (dram_mr0 << 16);
      v7[0x1208038] = dram_mr3 | (dram_mr2 << 16);
      if ( dram_type == 4 )
      {
        v7[0x120803A] = dram_mr5 | (dram_mr4 << 16);
        v7[0x120803B] = dram_mr6;
      }
    }
    else
    {
      v7[0x1208037] = dram_mr2 | (dram_mr1 << 16);
      v7[0x1208038] = dram_mr3 << 16;
      if ( dram_type == 8 )
      {
        v7[0x120803A] = dram_mr12 | (dram_mr11 << 16);
        v7[0x120803B] = dram_mr14 | (dram_mr22 << 16);
      }
    }
    v7[18907197] = v7[18907197] & 0xFFFFF00F | 0x660;
    if ( (dram_tpr13 & 0x20) != 0 )
      v36 = v44 | 0x2000000 | (v43 << 16);
    else
      v36 = (v44 - 1) | 0x2000000 | ((v43 - 1) << 16);
    --v1;
    v7[0x1208064] = v36 | 0x808000;
    v7[0x1208065] = 1049090;
    v7[18907161] = v60 | (v53 << 16);
  }
}

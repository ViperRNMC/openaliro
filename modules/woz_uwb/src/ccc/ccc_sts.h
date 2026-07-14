/** @file ccc_sts.h — load a CCC ranging PPDU's STS key + IV into the DW3000 STS engine. */

#ifndef CCC_STS_H
#define CCC_STS_H

#include <stdint.h>

#include "ccc_kdf.h"

/** Load one PPDU's STS key + IV into the DW3000 STS registers (chip must be idle). */
int ccc_sts_apply(const uint8_t dursk[CCC_DURSK_LEN],
		  const uint8_t sts_v[CCC_STS_V_LEN]);

#endif /* CCC_STS_H */

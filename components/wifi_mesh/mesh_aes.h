#ifndef __MESH_AES_H__
#define __MESH_AES_H__
#include "mesh_common.h"

#define  MESH_AES_KEY_LEN          (16)
#define  MESH_AES_BLOCK_SIZE       (16)

int mesh_aes_enc(unsigned char *key, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen);
int mesh_aes_dec(unsigned char *key, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen);

int mesh_aes_enc_by_bssid(unsigned char *bssid, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen);
int mesh_aes_dec_by_bssid(unsigned char *bssid, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen);

#endif

#include "mesh_aes.h"
#if 0 //def CONFIG_AES
#include "../include/drivers/aes/aes.h"

int mesh_aes_enc(unsigned char *key, unsigned char *in, unsigned int in_len, unsigned char *out, unsigned int max_len)
{
    unsigned char temp[MESH_AES_BLOCK_SIZE]={0};
    unsigned char padding_size;
    unsigned int i;

    if(!key || !in || !out || in_len > max_len) {
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }

    drv_aes_lock();
    drv_aes_ecb_setkey(key, MESH_AES_BLOCK_SIZE*8, DRV_AES_MODE_ENC);
    for (i = 0; i < in_len /MESH_AES_BLOCK_SIZE; i++) {
        drv_aes_crypt(&in[i *MESH_AES_BLOCK_SIZE ], &out[i * MESH_AES_BLOCK_SIZE]);
    }

    padding_size = MESH_AES_BLOCK_SIZE - in_len % MESH_AES_BLOCK_SIZE;
    memcpy(temp, &in[i * MESH_AES_BLOCK_SIZE], MESH_AES_BLOCK_SIZE - padding_size);
    memset(&temp[MESH_AES_BLOCK_SIZE - padding_size], padding_size, padding_size);
    drv_aes_crypt(temp, &out[i * MESH_AES_BLOCK_SIZE]);
    drv_aes_unlock();
    return in_len + padding_size;
}

int mesh_aes_dec(unsigned char *key, unsigned char *in, unsigned int in_len, unsigned char *out, unsigned int max_len)
{
    unsigned char temp[MESH_AES_BLOCK_SIZE]={0};
    unsigned char padding_size;
    unsigned int i;

    if(!key || !in || !out || in_len > max_len) {
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }

    drv_aes_lock();
    drv_aes_ecb_setkey(key, MESH_AES_KEY_LEN*8, DRV_AES_MODE_DEC);
    for (i = 0; i < in_len / MESH_AES_BLOCK_SIZE; i++) {
        drv_aes_crypt(&in[i * MESH_AES_BLOCK_SIZE], &out[i * MESH_AES_BLOCK_SIZE]);
    }
    drv_aes_unlock();

    padding_size = out[in_len -1];
    memset(temp, padding_size, padding_size);
    if (memcmp(temp, &out[in_len - padding_size], padding_size) != 0) {
        mesh_e("%s %d", __func__, __LINE__);
        return -2;
    }

    return in_len -= padding_size;
}

#else
int mesh_aes_enc(unsigned char *key, unsigned char *in, unsigned int in_len, unsigned char *out, unsigned int max_len)
{
    if (max_len < in_len) {
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }
    memcpy(out, in, in_len);
    return in_len;
}

int mesh_aes_dec(unsigned char *key, unsigned char *in, unsigned int in_len, unsigned char *out, unsigned int max_len)
{
    if (max_len < in_len) {
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }

   // memcpy(out, in, in_len);
    return in_len;
}

#endif

int mesh_aes_enc_by_bssid(unsigned char *bssid, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen)
{
    unsigned char key[MESH_AES_KEY_LEN*2];
#if 0
    if (!bssid || hal_hash_sha256(bssid, MESH_MAC_AADR_LEN, key)){
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }
#endif
    return mesh_aes_enc(key, in, inlen, out, outlen);
}
int mesh_aes_dec_by_bssid(unsigned char *bssid, unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen)
{
    unsigned char key[MESH_AES_KEY_LEN*2];
#if 0
    if (!bssid || hal_hash_sha256(bssid, MESH_MAC_AADR_LEN, key)){
        mesh_e("%s %d", __func__, __LINE__);
        return -1;
    }
#endif
    return mesh_aes_dec(key, in, inlen, out, outlen);
}


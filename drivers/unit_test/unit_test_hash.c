#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "FreeRTOS.h"
#include "task.h"
#include "oshal.h"
#include "hal_hash.h"


/**Length after calculating HASH*/
#define HASH_256_SUM_LEN		32
#define HASH_512_SUM_LEN		64

/**Message to be tested*/
static const unsigned char utest_hash_data_1[] = "12345678";
static const unsigned char utest_hash_data_2[] = "abcdefghijklmnopqrstuvwxyz00";
static const unsigned char utest_hash_data_3[] = "12345678abcdefghijklmnopqrstuvwxyz"
	"12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqrstuvwxyz";

/**The value of test data 1 after SHA256 calculation*/
static const unsigned char utest_sha256_sum_1[] = 
{
	0xef, 0x79, 0x7c, 0x81, 0x18, 0xf0, 0x2d, 0xfb, 
	0x64, 0x96, 0x07, 0xdd, 0x5d, 0x3f, 0x8c, 0x76, 
	0x23, 0x04, 0x8c, 0x9c, 0x06, 0x3d, 0x53, 0x2c, 
	0xc9, 0x5c, 0x5e, 0xd7, 0xa8, 0x98, 0xa6, 0x4f
};

/**The value of test data 2 after SHA256 calculation*/
static const unsigned char utest_sha256_sum_2[] = 
{
	0x5b, 0xb5, 0x32, 0x55, 0x07, 0xaf, 0x99, 0x12,
	0x81, 0x84, 0x84, 0x7b, 0xaf, 0xe0, 0x2a, 0x30,
	0xa1, 0x91, 0xae, 0xda, 0xa8, 0xba, 0xb8, 0x56,
	0x98, 0x07, 0x5e, 0xa6, 0x8e, 0x7b, 0x63, 0x2a
};


/**The value of test data 3 after SHA256 calculation*/
static const unsigned char utest_sha256_sum_3[] = 
{
	0x61, 0xa8, 0x9d, 0x8c, 0x04, 0x3d, 0xc9, 0xd9,
	0x54, 0xfc, 0x86, 0x56, 0xee, 0xe2, 0xbc, 0x42,
	0xd8, 0x5b, 0xb5, 0x31, 0xfe, 0x69, 0x4e, 0x6c,
	0xbd, 0x48, 0x23, 0xcb, 0x51, 0x05, 0x71, 0x31
};

/**The value of test data 1 after SHA512 calculation*/
static const unsigned char utest_sha512_sum_1[] = 
{
	0xfa, 0x58, 0x5d, 0x89, 0xc8, 0x51, 0xdd, 0x33,
	0x8a, 0x70, 0xdc, 0xf5, 0x35, 0xaa, 0x2a, 0x92,
	0xfe, 0xe7, 0x83, 0x6d, 0xd6, 0xaf, 0xf1, 0x22,
	0x65, 0x83, 0xe8, 0x8e, 0x09, 0x96, 0x29, 0x3f,
	0x16, 0xbc, 0x00, 0x9c, 0x65, 0x28, 0x26, 0xe0,
	0xfc, 0x5c, 0x70, 0x66, 0x95, 0xa0, 0x3c, 0xdd,
	0xce, 0x37, 0x2f, 0x13, 0x9e, 0xff, 0x4d, 0x13,
	0x95, 0x9d, 0xa6, 0xf1, 0xf5, 0xd3, 0xea, 0xbe
};

/**The value of test data 2 after SHA512 calculation*/
static const unsigned char utest_sha512_sum_2[] = 
{
	0xd2, 0xa4, 0xe3, 0x4a, 0x9b, 0x3c, 0xe2, 0xd0,
	0xa4, 0x0b, 0x0f, 0x51, 0x7d, 0x26, 0xcc, 0x82,
	0x7e, 0x91, 0x6c, 0x40, 0x45, 0x42, 0x21, 0xaa,
	0xaa, 0xfe, 0x55, 0x63, 0x49, 0x15, 0x5a, 0x6b,
	0xe3, 0x54, 0x02, 0xf9, 0xae, 0x9f, 0xc5, 0x94,
	0xf6, 0x39, 0xd3, 0x8c, 0xc2, 0x95, 0x22, 0xe5,
	0x7e, 0xb4, 0xde, 0x0c, 0xe9, 0xf2, 0x50, 0xc3,
	0x28, 0x31, 0xf4, 0x15, 0x16, 0xea, 0xf5, 0xce
};


/**The value of test data 3 after SHA512 calculation*/
static const unsigned char utest_sha512_sum_3[] = 
{
	0x78, 0x75, 0xbb, 0x5a, 0x55, 0x23, 0xe8, 0xdc,
	0x4c, 0xa1, 0xe9, 0x5b, 0x90, 0x5a, 0x7a, 0x72,
	0x3c, 0x20, 0x39, 0x40, 0x7e, 0xcc, 0x85, 0x00,
	0xa0, 0xda, 0xc3, 0xe3, 0x3e, 0xe7, 0x9e, 0x93,
	0x98, 0xde, 0x10, 0x9b, 0x1c, 0xbe, 0x6e, 0xb0,
	0x77, 0xfc, 0xbf, 0x84, 0xaa, 0x8b, 0xd7, 0xe7,
	0x03, 0x1e, 0x1b, 0x74, 0xd9, 0x97, 0x57, 0xd0,
	0xcd, 0xf2, 0x79, 0x46, 0xb4, 0x37, 0x6f, 0xa3
};



static int utest_hash_sha256(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned char output[HASH_512_SUM_LEN];   ///< Array of output data
	unsigned int data_len1, data_len2, data_len3;

	data_len1 = sizeof(utest_hash_data_1) - 1;   ///< Calculate the length of the input data_1
	hal_hash_sha256(utest_hash_data_1, data_len1, output);   ///< Perform SHA256 calculation on test data_1

	if (memcmp(utest_sha256_sum_1, output, HASH_256_SUM_LEN) == 0)   ///< Compare and verify the data after SHA256 calculation
	{
    	os_printf(LM_CMD,LL_INFO,">> sha256_test data-1 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test data-1 failed!\r\n");
	}

	data_len2 = sizeof(utest_hash_data_2) - 1;   ///< Calculate the length of the input data_2
	hal_hash_sha256(utest_hash_data_2, data_len2, output);   ///<Perform SHA256 calculation on test data_2

	if (memcmp(utest_sha256_sum_2, output, HASH_256_SUM_LEN) == 0)   ///< Compare and verify the data after SHA256 calculation
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test data-2 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test data-2 failed!\r\n");
	}

	data_len3 = sizeof(utest_hash_data_3) - 1;   ///< Calculate the length of the input data_3
	hal_hash_sha256(utest_hash_data_3, data_len3, output);   ///< Perform SHA256 calculation on test data_3

	if (memcmp(utest_sha256_sum_3, output, HASH_256_SUM_LEN) == 0)   ///< Compare and verify the data after SHA256 calculation
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test data-3 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test data-3 failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_hash, sha256, utest_hash_sha256, "unit test hash sha256", "ut_hash sha256");


static int utest_hash_sha512(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned char output[HASH_512_SUM_LEN];   ///< Array of output data
	unsigned int data_len1, data_len2, data_len3;

	data_len1 = sizeof(utest_hash_data_1) - 1;   ///< Calculate the length of the input data_1
	hal_hash_sha512(utest_hash_data_1, data_len1, output);   ///< Perform SHA512 calculation on test data_1

	if (memcmp(utest_sha512_sum_1, output, HASH_512_SUM_LEN) == 0)   ///< Compare and verify the data after SHA512 calculation
	{
    	os_printf(LM_CMD,LL_INFO,">> sha512_test data-1 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test data-1 failed!\r\n");
	}

	data_len2 = sizeof(utest_hash_data_2) - 1;   ///< Calculate the length of the input data_2
	hal_hash_sha512(utest_hash_data_2, data_len2, output);   ///< Perform SHA512 calculation on test data_2

	if (memcmp(utest_sha512_sum_2, output, HASH_512_SUM_LEN) == 0)   ///< Compare and verify the data after SHA512 calculation
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test data-2 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test data-2 failed!\r\n");
	}
  
	data_len3 = sizeof(utest_hash_data_3) - 1;   ///< Calculate the length of the input data_3
	hal_hash_sha512(utest_hash_data_3, data_len3, output);   ///< Perform SHA512 calculation on test data_3

	if (memcmp(utest_sha512_sum_3, output, HASH_512_SUM_LEN) == 0)   ///< Compare and verify the data after SHA512 calculation
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test data-3 pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test data-3 failed!\r\n");
	}

	return 0;
}

CLI_SUBCMD(ut_hash, sha512, utest_hash_sha512, "unit test hash sha512", "ut_hash sha512");


static const struct
{
	char *data;
	unsigned char hash[32];
} 
tests[] =
{
	{
		"abc",
		{
			0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
			0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
			0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
			0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
		}
	},
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		{
			0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
			0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
			0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
			0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
		}
	}
};


static int case0_len0 = 64;
static unsigned char case0_data0[] = 
{
	0x1f, 0x01, 0xe7, 0x4d, 0xfe, 0xa7, 0xee, 0xc8, 
	0x60, 0x59, 0x56, 0xf5, 0x98, 0x97, 0xcf, 0x44, 
	0xf5, 0x4e, 0x07, 0x81, 0xc3, 0xcb, 0xf0, 0x72, 
	0xde, 0x2d, 0x2a, 0xd7, 0xee, 0x3c, 0x0d, 0x22, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
};

static int case0_len1 = 2;
static unsigned char case0_data1[] = 
{
	0x01, 0x00
};


static int case0_len2 = 22;
static unsigned char case0_data2[] = 
{
	0x50, 0x61, 0x69, 0x72, 0x77, 0x69, 0x73, 0x65, 
	0x20, 0x6b, 0x65, 0x79, 0x20, 0x65, 0x78, 0x70, 
	0x61, 0x6e, 0x73, 0x69, 0x6f, 0x6e
};


static int case0_len3 = 76;
static unsigned char case0_data3[] = 
{
	0x00, 0x06, 0x06, 0x00, 0x00, 0x00, 0x90, 0x76, 
	0x9f, 0xd3, 0x43, 0xff, 0x01, 0xbf, 0xd1, 0x16, 
	0x5b, 0xd1, 0xb1, 0x3f, 0x4d, 0x47, 0xca, 0xd3, 
	0x0f, 0x91, 0xc5, 0xba, 0xea, 0x67, 0xcd, 0x8a, 
	0x08, 0x2e, 0xe9, 0xbf, 0x12, 0x60, 0x27, 0xe2, 
	0x71, 0x20, 0x66, 0x6e, 0xc4, 0x2c, 0x6e, 0xe0, 
	0x96, 0x34, 0xdf, 0xc7, 0xbb, 0xbe, 0xb6, 0x15, 
	0x53, 0x05, 0xc4, 0x9e, 0xbe, 0x28, 0xfd, 0xb7, 
	0xce, 0xe7, 0xeb, 0x7d, 0xbd, 0x9a, 0xc6, 0x3b, 
	0xf1, 0x83, 0x8b, 0xca, 
};


static int case0_len4 = 2;
static unsigned char case0_data4[] = 
{
	0x80, 0x01
};
/////////////////////////////////////////////////////////

static int case1_len0 = 64;
static unsigned char case1_data0[] = 
{
	0xbe, 0x69, 0xe6, 0x14, 0x04, 0x65, 0x22, 0xba, 
	0x45, 0xb1, 0x41, 0xad, 0xbb, 0xf7, 0xab, 0xb2, 
	0xbb, 0xd7, 0x82, 0xbe, 0xbe, 0xfc, 0x1b, 0xbb, 
	0x96, 0xd4, 0xc3, 0x76, 0xe4, 0xd7, 0x4f, 0xbe, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
};

static int case1_len1 = 2;
static unsigned char case1_data1[] = 
{
	0x00, 0x00
};

static int case1_len2 = 32;
static unsigned char case1_data2[] = 
{
	0x1b, 0x6f, 0xe9, 0x5b, 0x2e, 0x52, 0xa1, 0xe5, 
	0x33, 0xf6, 0xbf, 0x08, 0x3f, 0xe8, 0xfd, 0xa7, 
	0x4e, 0xbe, 0xc9, 0x0d, 0x72, 0xb6, 0x5a, 0x73, 
	0xf2, 0xaf, 0xe5, 0x1d, 0x53, 0x5b, 0x91, 0xfb 
};

static int case1_len3 = 64;
static unsigned char case1_data3[] = 
{
	0x66, 0xbf, 0x7c, 0xc3, 0xda, 0xe6, 0x77, 0x25, 
	0x76, 0x25, 0xe8, 0x60, 0xd4, 0x70, 0xb8, 0xf9, 
	0x85, 0x94, 0x0d, 0x00, 0xd4, 0x0d, 0x47, 0x9c, 
	0x14, 0x6e, 0x11, 0x16, 0xd9, 0xbc, 0xf9, 0x56, 
	0x93, 0xcc, 0x98, 0x84, 0x5d, 0xc0, 0x8c, 0x36, 
	0x7a, 0x6c, 0xfd, 0x87, 0xe9, 0x6d, 0xe8, 0xc0, 
	0x91, 0x2a, 0x5e, 0x82, 0x52, 0x16, 0x1f, 0x8e, 
	0xf4, 0x5b, 0x1d, 0x32, 0x0e, 0x63, 0x34, 0xd5
};

static int case1_len4 = 32;
static unsigned char case1_data4[] = 
{
	0xf4, 0x59, 0x10, 0x6d, 0x4c, 0xf7, 0xe0, 0x7b, 
	0xee, 0xee, 0xd6, 0x2e, 0xd2, 0x65, 0x32, 0x66, 
	0xc7, 0xaf, 0x1f, 0x41, 0x1a, 0xf0, 0x10, 0x56, 
	0x89, 0x55, 0x9e, 0x50, 0x89, 0xdc, 0x99, 0xe9
};

static int case1_len5 = 64;
static unsigned char case1_data5[] = 
{
	0x31, 0x94, 0x92, 0x91, 0x6b, 0x2a, 0xfd, 0xb5, 
	0x75, 0x8d, 0x9a, 0xfb, 0x4c, 0x7c, 0x55, 0x10, 
	0x38, 0xbd, 0x34, 0x1a, 0x3b, 0xdf, 0xe1, 0x09, 
	0xf4, 0x2f, 0x8c, 0xb0, 0x57, 0x2f, 0x09, 0x04, 
	0x63, 0x0d, 0x66, 0x2b, 0x9c, 0xd7, 0xa4, 0xe4, 
	0x73, 0xa0, 0xbb, 0x57, 0x2d, 0x8d, 0x70, 0xfd, 
	0x14, 0x99, 0xb8, 0xfe, 0x2f, 0xb4, 0xd1, 0xd1, 
	0x54, 0x70, 0x1a, 0xfa, 0x75, 0x2e, 0x34, 0x6c
};



/*  result ---case-1
	0x9e, 0x09, 0x43, 0xe7, 0xb3, 0x58, 0x47, 0x87, 
	0x30, 0x4b, 0x4a, 0xce, 0x44, 0xa4, 0x48, 0x38, 
	0xbb, 0x86, 0x56, 0xfe, 0x54, 0x93, 0x57, 0xcf, 
	0x9e, 0x4b, 0x53, 0x89, 0x84, 0xa6, 0xc8, 0xee, 
*/

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DUMP_LINE_SIZE			8

extern int drv_sha256_vector(unsigned int num_elem, const unsigned char * addr[], const size_t * plength, unsigned char * output);
extern void cli_hexdump(const void * addr, unsigned int size);
extern int sha256_vector(size_t num_elem, const unsigned char *addr[], const size_t *len,  unsigned char *mac);

void cli_hexdump(const void * addr, unsigned int size)
{
	const unsigned char * c = addr;

	os_printf(LM_CMD,LL_INFO, "Dumping %u bytes from %p:\r\n", size, addr);

	while (size)
	{
		int i;

		for (i=0; i<DUMP_LINE_SIZE; i++)
		{
			if (i < size)
			{
				os_printf(LM_CMD,LL_INFO,">> 0x%02x, ", c[i]);
			}
			else
			{
				os_printf(LM_CMD,LL_INFO, "    ");
			}
		}

		os_printf(LM_CMD,LL_INFO, "\r\n");

		c += DUMP_LINE_SIZE;

		if (size <= DUMP_LINE_SIZE)
		{
			break;
		}

		size -= DUMP_LINE_SIZE;
	}
}

static int utest_sha256(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned int i;
	unsigned char hash[32];
	const unsigned char *addr[2];
	size_t len[2];
	//unsigned char *key;

	const unsigned char  *_addr[6];
	int _len[6];

#if 1
	for (i = 0; i < ARRAY_SIZE(tests); i++)
	{
	    	os_printf(LM_CMD,LL_INFO,">> SHA256 test case %d: ", i+1);

		addr[0] = (unsigned char *) tests[i].data;
		len[0] = strlen(tests[i].data);
		hal_hash_sha256(addr[0], len[0], hash);
		
		if (memcmp(hash, tests[i].hash, 32) != 0)
		{
			os_printf(LM_CMD,LL_INFO," FAIL\r\n");
		}
		else
		{
			os_printf(LM_CMD,LL_INFO," OK\r\n");
		}

		if (len[0])
		{
			addr[0] = (unsigned char *) tests[i].data;
			len[0] = 1;
			addr[1] = (unsigned char *) tests[i].data + 1;
			len[1] = strlen(tests[i].data) - 1;
			drv_sha256_vector(2, addr, len, hash);
			if (memcmp(hash, tests[i].hash, 32) != 0)
			{
				os_printf(LM_CMD,LL_INFO," FAIL\r\n");
			} 
			else
			{
				os_printf(LM_CMD,LL_INFO," OK\r\n");
			}
		}
	}

	_addr[0] = case0_data0;
	_addr[1] = case0_data1;
	_addr[2] = case0_data2;
	_addr[3] = case0_data3;
	_addr[4] = case0_data4;

	_len[0] = case0_len0;
	_len[1] = case0_len1;
	_len[2] = case0_len2;
	_len[3] = case0_len3;
	_len[4] = case0_len4;

	drv_sha256_vector(5, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 32);

	sha256_vector(5, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 32);
#endif

	_addr[0] = case1_data0;
	_addr[1] = case1_data1;
	_addr[2] = case1_data2;
	_addr[3] = case1_data3;
	_addr[4] = case1_data4;
	_addr[5] = case1_data5;

	_len[0] = case1_len0;
	_len[1] = case1_len1;
	_len[2] = case1_len2;
	_len[3] = case1_len3;
	_len[4] = case1_len4;
	_len[5] = case1_len5;

	drv_sha256_vector(6, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 32);

	sha256_vector(6, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 32);


	return 0;
}

CLI_SUBCMD(ut_hash, vector256, utest_sha256, "unit test hash sha256 vector", "ut_hash vector");



static const struct
{
	char *data;
	unsigned char hash[64];
} 
tests512[] =
{
	{
		"abc",
		{
			0xDD, 0xAF, 0x35, 0xA1, 0x93, 0x61, 0x7A, 0xBA,
     		0xCC, 0x41, 0x73, 0x49, 0xAE, 0x20, 0x41, 0x31,
      		0x12, 0xE6, 0xFA, 0x4E, 0x89, 0xA9, 0x7E, 0xA2,
     		0x0A, 0x9E, 0xEE, 0xE6, 0x4B, 0x55, 0xD3, 0x9A,
      		0x21, 0x92, 0x99, 0x2A, 0x27, 0x4F, 0xC1, 0xA8,
      		0x36, 0xBA, 0x3C, 0x23, 0xA3, 0xFE, 0xEB, 0xBD,
      		0x45, 0x4D, 0x44, 0x23, 0x64, 0x3C, 0xE8, 0x0E,
      		0x2A, 0x9A, 0xC9, 0x4F, 0xA5, 0x4C, 0xA4, 0x9F
		}
	},

	{
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
      	"hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
		{
			0x8E, 0x95, 0x9B, 0x75, 0xDA, 0xE3, 0x13, 0xDA,
      		0x8C, 0xF4, 0xF7, 0x28, 0x14, 0xFC, 0x14, 0x3F,
      		0x8F, 0x77, 0x79, 0xC6, 0xEB, 0x9F, 0x7F, 0xA1,
      		0x72, 0x99, 0xAE, 0xAD, 0xB6, 0x88, 0x90, 0x18,
      		0x50, 0x1D, 0x28, 0x9E, 0x49, 0x00, 0xF7, 0xE4,
      		0x33, 0x1B, 0x99, 0xDE, 0xC4, 0xB5, 0x43, 0x3A,
      		0xC7, 0xD3, 0x29, 0xEE, 0xB6, 0xDD, 0x26, 0x54,
      		0x5E, 0x96, 0xE5, 0x5B, 0x87, 0x4B, 0xE9, 0x09 
		}
	},

	{
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
      	"hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstutu",
		{
			0xd3, 0x23, 0xc1, 0x99, 0x54, 0x94, 0xe1, 0x2b, 
			0xb9, 0x9b, 0x2c, 0xad, 0xb7, 0xf3, 0x83, 0x0a, 
			0x8a, 0x5f, 0x7d, 0xdb, 0x17, 0x44, 0x2f, 0x3b, 
			0xbd, 0xf1, 0x1d, 0xad, 0xcb, 0xc4, 0xb6, 0x4b, 
			0xf9, 0x0c, 0xe5, 0x44, 0x8c, 0xdf, 0xf3, 0xd2, 
			0x34, 0x1e, 0x05, 0xf8, 0x4b, 0x29, 0x03, 0x9c, 
			0x69, 0x7b, 0x16, 0x47, 0x32, 0x63, 0x73, 0x1c, 
			0x44, 0x9f, 0xa3, 0xc2, 0x2c, 0xfd, 0xbc, 0x32
		}
	},

	{
		"qwertyuiopasdfghjklzxcvbnmqwertyuiopasdfghjklzxcvbnmqwertyuiopasdfghjklzxcvbnm"
			"qwertyuiopasdfghjklzxcvbnmqwertyuiopasdfghjklzxcvbnm",
		{
			0x26, 0x4a, 0xd8, 0x8d, 0x84, 0xe9, 0x49, 0x7d,
			0xfa, 0x5a, 0x9f, 0x6e, 0x58, 0x3e, 0x9f, 0x8a,
			0x99, 0x90, 0x9e, 0xf3, 0xe7, 0xb8, 0xce, 0x78, 
			0xf6, 0xa9, 0xdb, 0x75, 0x74, 0x68, 0xeb, 0x34, 
			0x78, 0xc4, 0xab, 0x58, 0x14, 0x84, 0xf9, 0x0a, 
			0x41, 0xd6, 0x98, 0x0d, 0x04, 0x24, 0xc7, 0x9d, 
			0x5e, 0xcd, 0x08, 0xd2, 0xaa, 0xdc, 0xac, 0xe5, 
			0xb0, 0x18, 0xb5, 0x28, 0x6d, 0x7a, 0x6a, 0x57
		}
	},
	
};

extern int drv_sha512_vector(unsigned int num_elem, const unsigned char * addr[], const size_t * plength, unsigned char * output);
extern int sha512_vector(size_t num_elem, const unsigned char *addr[], const size_t *len,  unsigned char *mac);

static int utest_sha512(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned int i;
	unsigned char hash[64];
	const unsigned char *addr[2];
	size_t len[2];
	//unsigned char *key;

	//const unsigned char  *_addr[6];
	//int _len[6];


	for (i = 0; i < ARRAY_SIZE(tests512); i++)
	{
	    os_printf(LM_CMD,LL_INFO,">> SHA512 test case %d: ", i+1);

		addr[0] = (unsigned char *) tests512[i].data;
		len[0] = strlen(tests512[i].data);
		hal_hash_sha512(addr[0], len[0], hash);
		
		if (memcmp(hash, tests512[i].hash, 64) != 0)
		{
			os_printf(LM_CMD,LL_INFO," FAIL\r\n");
		}
		else
		{
			os_printf(LM_CMD,LL_INFO," OK\r\n");
		}

		if (len[0])
		{
		
			addr[0] = (unsigned char *) tests512[i].data;
			len[0] = 1;
			addr[1] = (unsigned char *) tests512[i].data + 1;
			len[1] = strlen(tests512[i].data) - 1;
		
			drv_sha512_vector(2, addr, len, hash);
		
			if (memcmp(hash, tests512[i].hash, 64) != 0)
			{
				os_printf(LM_CMD,LL_INFO," FAIL\r\n");
			} 
			else
			{
				os_printf(LM_CMD,LL_INFO," OK\r\n");
			}
		
		}

	}

	

/*	_addr[0] = case0_data0;
	_addr[1] = case0_data1;
	_addr[2] = case0_data2;
	_addr[3] = case0_data3;
	_addr[4] = case0_data4;

	_len[0] = case0_len0;
	_len[1] = case0_len1;
	_len[2] = case0_len2;
	_len[3] = case0_len3;
	_len[4] = case0_len4;

	drv_sha512_vector(5, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 64);

	//sha512_vector(5, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 64);


	_addr[0] = case1_data0;
	_addr[1] = case1_data1;
	_addr[2] = case1_data2;
	_addr[3] = case1_data3;
	_addr[4] = case1_data4;
	_addr[5] = case1_data5;

	_len[0] = case1_len0;
	_len[1] = case1_len1;
	_len[2] = case1_len2;
	_len[3] = case1_len3;
	_len[4] = case1_len4;
	_len[5] = case1_len5;

	drv_sha512_vector(6, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 64);

	//sha512_vector(6, _addr, (const size_t *)_len, hash);

	cli_hexdump(hash, 64);
*/

	return 0;
}



CLI_SUBCMD(ut_hash, vector512, utest_sha512, "unit test hash sha512 vector", "ut_hash vector");



static const unsigned char utest_hash_sha256_buf[] = "12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqrstuv"
												  "wxyz12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqr"
												  "stuvwxyz12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmn";

static const unsigned char utest_hash_sha256_sum[] = 
{
	0xfe, 0xdd, 0x66, 0xab, 0xe6, 0x40, 0x00, 0x53,
	0xf8, 0x26, 0x4f, 0xcc, 0xfa, 0xdb, 0x4c, 0x48,
	0xbf, 0x1b, 0xae, 0x04, 0xcb, 0x92, 0x71, 0x96,
	0x53, 0xb9, 0xfb, 0x0d, 0xfc, 0x43, 0xde, 0x64
};

static int utest_hash_sha256_64mul(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned int data_len;
	unsigned char output[HASH_256_SUM_LEN];

	data_len = sizeof(utest_hash_sha256_buf) - 1;

	os_printf(LM_CMD,LL_INFO, "data_len = %d\r\n", data_len);
	
	hal_hash_sha256(utest_hash_sha256_buf, data_len, output);

	if (memcmp(utest_hash_sha256_sum, output, HASH_256_SUM_LEN) == 0)   
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test buf  pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha256_test buf failed!\r\n");
	}

	return 0;
}
	 
CLI_SUBCMD(ut_hash, sha256_64mul, utest_hash_sha256_64mul, "unit test hash sha256", "ut_hash sha256");


static const unsigned char  utest_hash_sha512_buf[] = "12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqrstuv"
												  "wxyz12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqr"
												  "12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqrstuv"
												  "wxyz12345678abcdefghijklmnopqrstuvwxyz12345678abcdefghijklmnopqr";

static const unsigned char utest_hash_sha512_sum[] = 
{
	0x1d, 0x5c, 0xb3, 0xe9, 0xfc, 0x31, 0x4b, 0x43,
	0xe7, 0x72, 0x82, 0x9a, 0xa8, 0x62, 0x83, 0x7c,
	0xbd, 0x33, 0xce, 0x02, 0x98, 0x71, 0x59, 0x70,
	0xb2, 0xf7, 0xb6, 0x1f, 0xc9, 0x41, 0xd7, 0x15,
	0xe5, 0x5b, 0x40, 0x72, 0x06, 0xbe, 0xa9, 0x15,
	0x12, 0x02, 0x39, 0xda, 0x98, 0x38, 0xe5, 0x22,
	0xb7, 0xd5, 0x68, 0x10, 0x3f, 0x3a, 0x77, 0xd5,
	0x8a, 0x04, 0xc3, 0xd7, 0xc3, 0x45, 0xf2, 0x4f
};

static int utest_hash_sha512_128mul(cmd_tbl_t *t, int argc, char *argv[])
{
	unsigned int data_len;
	unsigned char output[HASH_512_SUM_LEN];

	data_len = sizeof(utest_hash_sha512_buf) - 1;

	os_printf(LM_CMD,LL_INFO, "data_len = %d\r\n", data_len);
	
	hal_hash_sha512(utest_hash_sha512_buf, data_len, output);

	if (memcmp(utest_hash_sha512_sum, output, HASH_512_SUM_LEN) == 0)   
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test buf  pass!\r\n");
	}
	else
	{
		os_printf(LM_CMD,LL_INFO,">> sha512_test buf failed!\r\n");
	}
	return 0;
}
	 
CLI_SUBCMD(ut_hash, sha512_128mul, utest_hash_sha512_128mul, "unit test hash sha512", "ut_hash sha512");

CLI_CMD(ut_hash, NULL, "unit test hash", "test_hash");







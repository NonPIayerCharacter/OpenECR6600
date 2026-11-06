
#include "cli.h"
#include "lfs_port.h"

void *fs_handle;




static int cmd_lfs_test(int argc, char *argv[])
{
    int ret;
    unsigned int count = 0;
    lfs_file_t file;

	if (fs_handle == NULL)
	{
		fs_handle = lfs_init();
        if (fs_handle == NULL)
        {
            os_printf(LM_APP, LL_ERR,"unit test lfs, init failed\r\n");
            return 0;
        }
	}

    ret = lfs_file_open(fs_handle, &file, "test", LFS_O_RDWR | LFS_O_CREAT);
    if (ret)
    {
        os_printf(LM_APP, LL_ERR,"unit test lfs, open err: %d\r\n", ret);
        return 0;
    }

    ret = lfs_file_read(fs_handle, &file, &count, sizeof(count));
    if (ret != 0 && ret != sizeof(count))
    {
        os_printf(LM_APP, LL_ERR,"unit test lfs, read err: %d\r\n", ret);
        lfs_file_close(fs_handle, &file);
        return 0;
    }

    // update boot count
    count += 1;
    ret = lfs_file_rewind(fs_handle, &file);
    if (ret)
    {
        os_printf(LM_APP, LL_ERR,"unit test lfs, rewind err: %d\r\n", ret);
        lfs_file_close(fs_handle, &file);
        return 0;
    }

    ret = lfs_file_write(fs_handle, &file, &count, sizeof(count));
    if (ret != sizeof(count))
    {
        os_printf(LM_APP, LL_ERR,"unit test lfs, write err: %d\r\n", ret);
        lfs_file_close(fs_handle, &file);
        return 0;
    }

    // remember the storage is not updated until the file is closed successfully
    ret = lfs_file_close(fs_handle, &file);
    if (ret)
    {
        os_printf(LM_APP, LL_ERR,"unit test lfs, close err: %d\r\n", ret);
        return 0;
    }
	
    os_printf(LM_APP, LL_ERR,"unit test lfs, cnt=%d\r\n", count);
	return 0;
}


CLI_CMD(ut_lfs, cmd_lfs_test, "littlefs test", "lfs_test");

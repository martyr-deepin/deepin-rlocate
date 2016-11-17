/* 
 * rlocate.c
 * kernel module for 2.6.x kernels
 *
 * Copyright (C) 2004 Rasto Levrinc.
 *
 * Parts of the LSM implementation were taken from
 * (http://www.logic.at/staff/robinson/)     
 * Copyright (C) 2004 by Peter Robinson
 *   
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/version.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include <linux/security.h>
#include <linux/init.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/device.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/devfs_fs_kernel.h>
#endif

#include <linux/namei.h>
#include <linux/jiffies.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12)
#define class_create class_simple_create
#define class_destroy class_simple_destroy
#define class_device_create(a, b, c, d, e) class_simple_device_add(a, c, d, e);
#define class_device_destroy(a, b) class_simple_device_remove(b)
static struct class_simple *rlocate_class;
#else

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#define class_device_create(a, b, c, d, e) class_device_create(a, c, d, e); 
#endif

static struct class *rlocate_class;
#endif

MODULE_AUTHOR("Rasto Levrinc");
MODULE_DESCRIPTION("rlocate");
MODULE_LICENSE("GPL");

#define DEVICE_NAME     "rlocate"
/* 30 minutes timeout for daemon */
#define D_TIMEOUT       (60*30*HZ) 

#ifndef SETPROC_OPS
#define SETPROC_OPS(entry, ops) (entry)->proc_fops = &(ops)
#endif

/* string list */
struct string_list {
        struct list_head list;
        char *string;
};

inline static void add_string_entry(struct list_head *string_list_head, 
                                    const char *string);
inline static void remove_string_entry(struct string_list *string_entry);
inline static void remove_string_list(const struct list_head *string_list_head);
inline static void add_filenames_entry(char *path);

static rwlock_t proc_lock = RW_LOCK_UNLOCKED;

LIST_HEAD(filenames_list); 	      /* list of filenames with leading 
				 	 'm' or 'a' */
static char *EXCLUDE_DIR_ARG;         /* excluded directories */
static char *STARTING_PATH_ARG;       /* starting at path */
static char *OUTPUT_ARG;              /* default database */
static char ACTIVATED_ARG = '0';      /* activated 0 - not running, 
				                   1 - running 
                                                   d - disabled */
static unsigned char UPDATEDB_ARG = 0;/* updatedb counter. If it reaches 0, 
					 full update will be performed */
static unsigned long NEXT_D_TIMEOUT;  /* next daemon timeout */

static unsigned char RELOAD = '0';    /* Will be written as first line in the
					 dev file. It will be set to '1' if
					 mount or umount was called. */


/* device file */
static DECLARE_MUTEX(dev_mutex);


static int rlocate_dev_register( void );
static void rlocate_dev_unregister( void );
static int rlocate_dev_open( struct inode *, struct file * );
static int rlocate_dev_release( struct inode *, struct file * );
static loff_t rlocate_dev_llseek( struct file *filp, loff_t offset, int whence);
static int rlocate_dev_ioctl( struct inode *inode, struct file *filp, 
                              unsigned int cmd, unsigned long arg );
ssize_t rlocate_dev_write( struct file *filp, const char *buff, size_t count, 
                           loff_t *offp );
static ssize_t rlocate_dev_read( struct file *, char *, size_t, loff_t * );
//static unsigned int rlocate_dev_poll( struct file *filp, poll_table *wait);

static int Major;           /* holds major number from device */
static char *path_buffer;   /* path_buffer for d_path call */

static struct file_operations device_fops = {
        .owner = THIS_MODULE,
        .open           = rlocate_dev_open,
        .llseek         = rlocate_dev_llseek,
        .ioctl          = rlocate_dev_ioctl,
        .write          = rlocate_dev_write,
        .read           = rlocate_dev_read,
        .release        = rlocate_dev_release,
//        .poll           = rlocate_dev_poll,
};

/* lsm hooks */
static int rlocate_inode_create( struct inode *dir, struct dentry *dentry, 
                                 int mode);
static int rlocate_inode_mkdir( struct inode * dir, struct dentry * dentry, 
                                int mode );
static int rlocate_inode_link ( struct dentry * old_dentry, struct inode * dir, 
                                struct dentry * dentry);
static int rlocate_inode_symlink ( struct inode * dir, struct dentry * dentry, 
                                   const char *old_name);
static int rlocate_inode_mknod ( struct inode * dir, struct dentry * dentry,
                                 int mode, dev_t dev);
static int rlocate_inode_rename( struct inode * old_dir,
                                 struct dentry * old_dentry,
                                 struct inode * new_dir,
                                 struct dentry * new_dentry);

static int rlocate_sb_mount( char *dev_name, 
			     struct nameidata *nd, 
			     char *type, 
			     unsigned long flags, 
			     void *data);

//static int rlocate_sb_umount( struct vfsmount *mnt, int flags );

#ifdef RLOCATE_UPDATES
static int rlocate_inode_permission (struct inode *inode, int mask, 
                                     struct nameidata *nd);
#endif
//static wait_queue_head_t filenames_wq;

static struct security_operations rlocate_security_ops = {
        .inode_create =                 rlocate_inode_create,
        .inode_mkdir  =                 rlocate_inode_mkdir,
        .inode_link   =                 rlocate_inode_link,
        .inode_symlink=                 rlocate_inode_symlink,
        .inode_mknod  =                 rlocate_inode_mknod,
        .inode_rename =                 rlocate_inode_rename,
	.sb_mount     =			rlocate_sb_mount,
//	.sb_umount    =			rlocate_sb_umount,
#ifdef RLOCATE_UPDATES
        .inode_permission =            rlocate_inode_permission,
#endif
};

/* proc fs */
enum Option { NOTSET, UNKNOWN, VERSION, EXCLUDE_DIR, ACTIVATED, STARTING_PATH, 
              OUTPUT, UPDATEDB };
#define LINES_IN_PROC 6

/* proc_data is used for reading and writing to the proc fs */
struct proc_data {
        char        *pbuffer;
        int         pbuffer_pos;
        int         read_line;
        enum Option option;

};

static int proc_rlocate_open( struct inode *inode, struct file *file);
static ssize_t proc_rlocate_read( struct file *file,
                                  char *buffer,
                                  size_t len,
                                  loff_t *offset);
static void parse_proc(struct proc_data *pdata);
static ssize_t proc_rlocate_write( struct file *file,
                                   const char *buffer,
                                   size_t len,
                                   loff_t *offset );
static int proc_rlocate_close( struct inode *inode, struct file *file);

static struct file_operations proc_rlocate_ops = {
        .open    = proc_rlocate_open,
        .read    = proc_rlocate_read,
        .write   = proc_rlocate_write,
        .release = proc_rlocate_close,
};



/* --------------------------- Functions ----------------------------------- */

/* string_list functions *****************************************************/

/*
 * add_string_entry() adds 'string' to the string_list.
 */
inline void add_string_entry(struct list_head *string_list_head, 
                             const char *string)
{
        struct string_list *s;
        if ( (s = kmalloc(sizeof(struct string_list), GFP_KERNEL)) == NULL) {
                printk(KERN_ERR "rlocate: add_string_entry:"
                                " memory allocation error.\n");
        } else {
                if ((s->string = 
                     kmalloc(strlen(string)+1, GFP_KERNEL)) == NULL) {
			printk(KERN_ERR "rlocate: add_string_entry: "
                                        "memory allocation error.\n");
                        kfree(s);
		} else {
			strcpy(s->string, string);
                        list_add_tail(&s->list, string_list_head);
		}
                
        }
}

/*
 * remove_string_entry() removes the STRING_ENTRY from string list and 
 * frees the memory.
 */
inline static void remove_string_entry(struct string_list *string_entry)
{
        list_del(&string_entry->list);
        kfree(string_entry->string);
        kfree(string_entry);
}

/*
 * remove_string_list() loops over the string list with specified list_head 
 * and removes all entries in the list.
 */
inline static void remove_string_list(const struct list_head *string_list_head)
{
        while (!list_empty(string_list_head)) {
                struct string_list *entry;
                entry = list_entry(string_list_head->next,
                                   struct string_list,
                                   list);
                remove_string_entry(entry);
        }
}

/*
 * add_filenames_entry() inserts PATH, that was created in the filesystem, to 
 * the filenames list. First character of path is 'a' or 'm', depending on if 
 * the filename was added or moved.
 */
inline static void add_filenames_entry(char *path) 
{
	//int starting_path_len;
	//int path_len = strlen(path) - 1; // -1 without first character

        // check path against STARTING_PATH_ARG
	/*
	// will be done in deamon for now

        if (*STARTING_PATH_ARG != '\0') {
		starting_path_len = strlen(STARTING_PATH_ARG);
		if (strncmp(STARTING_PATH_ARG, 
			    path + 1,
		            (starting_path_len - 1 == path_len ?
			     			path_len:starting_path_len)))
                        return; // this path is not in starting path
        }
	*/
        add_string_entry(&filenames_list, path);
        //wake_up_interruptible( &filenames_wq );
        return;
}

/* proc fs functions *********************************************************/

/* 
 * rlocate_init_fs() creates /proc/rlocate entry.
 */
static int rlocate_init_procfs(void)
{
        struct proc_dir_entry *p;

        p = create_proc_entry("rlocate", 00600, NULL);
        if (!p)
                return -ENOMEM;
        p->owner = THIS_MODULE;
        SETPROC_OPS(p, proc_rlocate_ops);
        return 0;
}

/*
 * proc_rlocate_open() initializes pdata structure.
 */
static int proc_rlocate_open( struct inode *inode, struct file *file) {
        struct proc_data *pdata;
        int ret;
        if ((file->private_data = kmalloc(sizeof(struct proc_data), 
                                          GFP_KERNEL)) == NULL) {
                ret = -ENOMEM;
                goto out;
        }
        memset(file->private_data, 0, sizeof(struct proc_data)); 
        pdata = (struct proc_data *)file->private_data;
        pdata->pbuffer = (char*)__get_free_page( GFP_KERNEL );
        if (!pdata->pbuffer) {
                ret = -ENOMEM;
                goto no_pbuffer;
        }
        pdata->option      = NOTSET;
        pdata->read_line   = 0;
        pdata->pbuffer_pos = 0;
        ret = 0;
        goto out;

no_pbuffer:
        kfree(file->private_data);
out:
        return ret;
}

/* 
 * proc_rlocate_close()  
 */
static int proc_rlocate_close( struct inode *inode, struct file *file)
{
        struct proc_data *pdata = (struct proc_data*)file->private_data;
        if (pdata->option!=NOTSET) {
                // in case there was no new line by writing
                pdata->pbuffer[pdata->pbuffer_pos]='\n';
                parse_proc(pdata);
        }
        free_page( (unsigned long)pdata->pbuffer);
        kfree(pdata);
        return 0;
}

/* 
 * proc_rlocate_read() prints out all the options with their arguments (one 
 * argument at a time) to the read buffer.
 */
static ssize_t proc_rlocate_read( struct file *file,
                                  char *buffer,
                                  size_t len,
                                  loff_t *offset)
{
        int i;
        char p;
        struct proc_data *pdata = (struct proc_data*)file->private_data;
        if( !pdata->pbuffer ) return -EINVAL;
        for (i = 0; i<len; i++) {
                if (pdata->pbuffer_pos == 0) { // beginning of the line
                        if (pdata->option == NOTSET) {
                                switch(pdata->read_line)
                                {
                                case 0:
                                        sprintf(pdata->pbuffer, "version: ");
                                        pdata->option = VERSION;
                                        break;
                                case 1:
                                        sprintf(pdata->pbuffer, "excludedir: ");
                                        pdata->option = EXCLUDE_DIR;
                                        break;
                                case 2:
                                        sprintf(pdata->pbuffer, "activated: ");
                                        pdata->option = ACTIVATED;
                                        break;
                                case 3:
                                        sprintf(pdata->pbuffer, "startingpath: ");
                                        pdata->option = STARTING_PATH;
                                        break;
                                case 4:
                                        sprintf(pdata->pbuffer, "output: ");
                                        pdata->option = OUTPUT;
                                        break;
                                case 5:
                                        sprintf(pdata->pbuffer, "updatedb: ");
                                        pdata->option = UPDATEDB;
                                        break;
                                }
                        }
                }
                p = pdata->pbuffer[pdata->pbuffer_pos];
                if (p == '\0') { 
                        if (pdata->pbuffer[pdata->pbuffer_pos-1] == '\n') {
                                pdata->option = NOTSET;
                        
                                pdata->read_line++;
                                if (pdata->read_line>=LINES_IN_PROC) {
                                        if (put_user( '\0', buffer+i ))
                                                return -EFAULT;
                                        break;
                                }
                        }
                        pdata->pbuffer_pos = 0;
                        switch(pdata->option) 
                        {
                        case VERSION:
                                sprintf(pdata->pbuffer, "%s\n", RL_VERSION);
                                break;
                        case EXCLUDE_DIR:
                                read_lock(&proc_lock);
                                sprintf(pdata->pbuffer, "%s\n", 
                                                EXCLUDE_DIR_ARG);
                                read_unlock(&proc_lock);
                                break;
                        case ACTIVATED:
                                read_lock(&proc_lock);
                                sprintf(pdata->pbuffer, "%c\n", 
                                                ACTIVATED_ARG);
                                read_unlock(&proc_lock);
                                break;
                        case STARTING_PATH:
                                read_lock(&proc_lock);
                                sprintf(pdata->pbuffer, "%s\n", 
						STARTING_PATH_ARG);
                                read_unlock(&proc_lock);
                                break;
                        case OUTPUT:
                                read_lock(&proc_lock);
                                sprintf(pdata->pbuffer, "%s\n", 
                                                OUTPUT_ARG);
                                read_unlock(&proc_lock);
                                break;
                        case UPDATEDB:
                                read_lock(&proc_lock);
                                sprintf(pdata->pbuffer, "%i\n", 
                                                UPDATEDB_ARG);
                                read_unlock(&proc_lock);
                                break;
                        case NOTSET:
                        case UNKNOWN:
                                break;
                        }
                        i--; // remove '\0'
                } else {
                        if (put_user( p, buffer+i ))
                                return -EFAULT;
                        pdata->pbuffer_pos++;
                }
        }
        *offset += i;
        return i;
}

/*
 * proc_set_option()
 */
inline void proc_set_option(const char *option_string, 
                            struct proc_data *pdata) 
{
        if (!strcmp(option_string, "excludedir")) {
                pdata->option   = EXCLUDE_DIR;
        } else if (!strcmp(option_string, "activated")) {
                pdata->option   = ACTIVATED;
        } else if (!strcmp(option_string, "startingpath")) {
                pdata->option   = STARTING_PATH;
        } else if (!strcmp(option_string, "output")) {
                pdata->option   = OUTPUT;
        } else if (!strcmp(option_string, "updatedb")) {
                pdata->option   = UPDATEDB;
        } else {
                printk(KERN_WARNING "unknown option: %s\n", option_string);
                pdata->option   = UNKNOWN;
        }
}

/*
 * parse_proc() parses a line like activated:0 written to /proc/rlocate
 */
static void parse_proc(struct proc_data *pdata)
{
        char *p;
        p = pdata->pbuffer + pdata->pbuffer_pos;
        // remove leading spaces or reduce more spaces to one.
        if ( *p == ' ' && (pdata->pbuffer_pos == 0 || *(p-1) == ' ')) {
                pdata->pbuffer_pos--;
                return;
        }
        switch (pdata->option) 
        {
        case NOTSET:
                if (*p == ':') {
                        // option string is in pbuffer

                        // remove space before ':'
                        if (pdata->pbuffer_pos>0 && *(p-1) == ' ')
                                p--;
                        *p = '\0';
                        proc_set_option(pdata->pbuffer, pdata);
                        pdata->pbuffer_pos = -1;
                } else if (*p == '\n') {
                        pdata->pbuffer_pos = -1;
                }
                break;
        case EXCLUDE_DIR:
                if ( *p == '\n' ) {
                        // the argument is in pbuffer
                        pdata->option = NOTSET;
                        *p = '\0';
                        write_lock(&proc_lock);
                        if (ACTIVATED_ARG == '1' 
                            && strcmp(EXCLUDE_DIR_ARG, pdata->pbuffer)) 
                                UPDATEDB_ARG = 0; /* full updatedb if eclude dir
                                                     has changed */
                        strcpy(EXCLUDE_DIR_ARG, pdata->pbuffer);
                        write_unlock(&proc_lock);
                        pdata->pbuffer_pos = -1;
                }
                break;
        case ACTIVATED:
                if ( *p == '\n' ) {
                        pdata->option = NOTSET;
                        *p = '\0';
                        if ( pdata->pbuffer[0] >= '0' && 
                             pdata->pbuffer[0] <= '1') {
                                write_lock(&proc_lock);
                                if(ACTIVATED_ARG == '1' && pdata->pbuffer[0]=='0')
                                        UPDATEDB_ARG = 0; /* full updatedb if
                                                             activated was 1 and 
                                                             changed to 0  */
                                ACTIVATED_ARG = pdata->pbuffer[0];
                                write_unlock(&proc_lock);
                        } 
                        pdata->pbuffer_pos = -1;
                }
                break;
        case STARTING_PATH:
                if ( *p == '\n' ) {
                        // the argument is in pbuffer
                        pdata->option = NOTSET;
                        // add trailing slash, if it's not there
                        if (p>pdata->pbuffer) {
                                if (*(p-1) == '/' || 
				    p - pdata->pbuffer >= PATH_MAX + 6) { 
                                        *p = '\0';
                                } else { 
                                        *p = '/';
                                        *(p+1) = '\0';
                                }
                        } else {
                                *p = '\0';
                        }
                        write_lock(&proc_lock);
                        if (ACTIVATED_ARG == '1' 
                            && strcmp(STARTING_PATH_ARG, pdata->pbuffer)) 
                                UPDATEDB_ARG = 0; /* full updatedb if starting 
                                                     path has changed */
                        strcpy(STARTING_PATH_ARG, pdata->pbuffer);

                        write_unlock(&proc_lock);
                        pdata->pbuffer_pos = -1;
                }
                break;
        case OUTPUT:
                if ( *p == '\n' ) {
                        // the argument is in pbuffer
                        pdata->option = NOTSET;
                        *p = '\0';
                        write_lock(&proc_lock);
                        if (ACTIVATED_ARG == '1' 
                            && strcmp(OUTPUT_ARG, pdata->pbuffer)) 
                                UPDATEDB_ARG = 0; /* full updatedb if output has
                                                     changed */
                        strcpy(OUTPUT_ARG, pdata->pbuffer);
                        write_unlock(&proc_lock);
                        pdata->pbuffer_pos = -1;
                }
                break;
        case UPDATEDB:
                if ( *p == '\n' ) {
                        pdata->option = NOTSET;
                        *p = '\0';
                        write_lock(&proc_lock);
                        UPDATEDB_ARG = simple_strtoul(pdata->pbuffer , NULL, 10);
                        write_unlock(&proc_lock);
                        pdata->pbuffer_pos = -1;
                }
                break;
        default:
                if ( *p=='\n' ) {
                        pdata->option = NOTSET;
                        pdata->pbuffer_pos = -1;
                }
                break;
        }
}

/*
 * proc_rlocate_write() fills lists with arguments from buffer
 */
static ssize_t proc_rlocate_write( struct file *file,
                                   const char *buffer,
                                   size_t len,
                                   loff_t *offset )
{
        int i;
        char p;
        struct proc_data *pdata = (struct proc_data*)file->private_data;
        for (i = 0; i<len; i++) {
                if (get_user( p, buffer + i))
                        return -EFAULT;
                if ( pdata->pbuffer_pos < (PAGE_SIZE-2)) { // buffer overflow
                        pdata->pbuffer[pdata->pbuffer_pos] = p;
                        parse_proc(pdata);
                        pdata->pbuffer_pos++;
                } else {
                        pdata->pbuffer[pdata->pbuffer_pos] = '\n';
                        pdata->pbuffer[pdata->pbuffer_pos+1] = '\0';
                }
        }
        *offset += i;
        return i;
}

/*
 * rlocate_exit_procfs()
 */
int rlocate_exit_procfs(void)
{
        remove_proc_entry("rlocate", 0);
        return 0;
}

/* device functions *********************************************************/


/* 
 * rlocate_dev_register()
 */
static int rlocate_dev_register(void)
{
        int ret = 0;
        Major = register_chrdev(0, DEVICE_NAME, &device_fops);
        if (Major < 0) {
                printk (KERN_ERR "rlocate: register_chrdev failed with %d\n", 
                                 Major);
                ret = Major;
                goto out;
        }
        printk (KERN_INFO "rlocate: registered device " 
                DEVICE_NAME " major: %d\n", Major);

        // sysfs 
        rlocate_class = class_create(THIS_MODULE, "rlocate");
        if (IS_ERR(rlocate_class)) {
                printk(KERN_ERR "Error creating rlocate class.\n");
                ret = PTR_ERR(rlocate_class);
                goto no_simple_class; 
        }
        class_device_create(rlocate_class, NULL, MKDEV(Major, 0), NULL, 
                                DEVICE_NAME);
        // devfs
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
        devfs_mk_cdev(MKDEV(Major, 0), S_IFCHR|S_IRUSR|S_IWUSR, DEVICE_NAME);
#endif
        goto out;

no_simple_class:
        unregister_chrdev(Major, DEVICE_NAME);
out:
        return ret;
}

/*
 * rlocate_dev_unregister()
 */
void rlocate_dev_unregister(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
        devfs_remove(DEVICE_NAME);
#endif
        class_device_destroy(rlocate_class, MKDEV(Major, 0));
        class_destroy(rlocate_class);
        unregister_chrdev(Major, DEVICE_NAME);
}

/*
 * rlocate_dev_open()
 */
static int rlocate_dev_open(struct inode *inode, struct file *file)
{
        down(&dev_mutex);
        return 0;
}

/*
 * rlocate_dev_release()
 */
static int rlocate_dev_release(struct inode *inode, struct file *file)
{
        up(&dev_mutex);
        return 0;
}

/*
 * rlocate_dev_llseek()
 */
static loff_t rlocate_dev_llseek( struct file *filp, loff_t offset, int whence)
{
        return -ESPIPE; /* unseekable */
}

/*
 * rlocate_dev_ioctl()
 */
static int rlocate_dev_ioctl( struct inode *inode, struct file *filp, 
                              unsigned int cmd, unsigned long arg )
{
        return 0; /* success */    
}

/*
 * rlocate_dev_write
 */
ssize_t rlocate_dev_write( struct file *filp, const char *buff, size_t count, 
                           loff_t *offp )
{
        return (ssize_t)0;
}

/* 
 * rlocate_dev_read() 
 * traverse the list of filenames and write them to the user buffer.
 * Remove every entry from the filenames list that has been written. 
 */
static ssize_t rlocate_dev_read(struct file *file, 
                           char *buffer, 
                           size_t length, 
                           loff_t *offset)
        {
	ssize_t bytes_read = 0;
        char *string_ptr;
        struct string_list *f_entry;

        /* daemon activity, set next timeout */
        NEXT_D_TIMEOUT = jiffies + D_TIMEOUT;
        if (ACTIVATED_ARG == 'd') {
                /* inserting of paths was disabled, reenable it */
                printk(KERN_INFO "rlocate: inserting of paths reenabled.\n");
                ACTIVATED_ARG = '1';
        }
        if ( list_empty(&filenames_list) ) { // put to sleep
                if( file->f_flags & O_NONBLOCK ) 
                        return -EWOULDBLOCK;
                //interruptible_sleep_on( &filenames_wq );
        }
        while ( (!list_empty(&filenames_list)) ) {
                f_entry = list_entry(filenames_list.next,
                                     struct string_list,
                                     list);
                string_ptr = f_entry->string;
		if (RELOAD == '1') {
			put_user('1', buffer++);
			put_user('\0', buffer++);
			bytes_read += 2;
			RELOAD = '0';
		}
                if ( length - bytes_read <= strlen(string_ptr)+1 ) { //+1 for \n
                        break;
                } else {
	                while (*string_ptr) {
		                put_user(*(string_ptr++), buffer++);
		                bytes_read++;
	                }
		        put_user('\0', buffer++);
                        bytes_read++;
                        remove_string_entry(f_entry);
                }
        }
        return bytes_read;
}

/*
 * rlocate_dev_poll
 */
/*
static unsigned int rlocate_dev_poll( struct file *filp, poll_table *wait)
{
        unsigned int mask = 0;

        poll_wait( filp, &filenames_wq, wait );
        if( !list_empty(&filenames_list) )
                mask |= POLLIN | POLLRDNORM;
        return mask;
}
*/

/*
 * get_path() return the path of a dentry with major and minor number instead
 * of mount point.
 */
inline static char * get_path(struct dentry *dentry, char *buffer, int buflen) {
	char * end = buffer + buflen;
	char * retval;
	int namelen;

	*--end = '\0';
	buflen--;

	retval = end - 1;
	*retval = '/';
	for (;;) {
		struct dentry *parent;
		if (IS_ROOT(dentry)) {
			goto mountroot;
		}	
		parent = dentry->d_parent;
		prefetch(parent);
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		dentry = parent;
	}
mountroot:
	namelen = strlen(dentry->d_sb->s_id);
	buflen -=namelen + 2;
	if (buflen < 0)
		goto Elong;
	*--end = ':';
	end -= namelen;
	memcpy(end, dentry->d_sb->s_id, namelen);
	retval = end - 1;// place for mode
	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}
/********************* SECURITY MODULE HOOKS *************************/

/* 
 * insert_path() finds the corresponding full path for a dentry and inserts 
 * it to the filenames_list. MODE which is 'a' or 'm' will be added to the 
 * beginning of the path. 
 */
inline static void insert_path(struct dentry *dentry, const char mode) 
{
        char *path;

        if (ACTIVATED_ARG != '1') {
                UPDATEDB_ARG = 0; // for full updatedb
                return;  // not activated or disabled
        }
        if (time_after(jiffies, NEXT_D_TIMEOUT)) {
                /* timeout, disable inserting of paths */
                if (ACTIVATED_ARG != 'd') {
                        ACTIVATED_ARG = 'd'; // disabled
                        printk(KERN_INFO "rlocate: daemon timeout: "
                                "inserting of paths disabled.\n");
                }
                return;
        }
	path = get_path(dentry, path_buffer, PATH_MAX + 8);
        if (!IS_ERR(path)) {
        	*path = mode;  // char mode: 'a' added, 'm' moved
                add_filenames_entry(path);
	} else 
        	printk(KERN_WARNING "rlocate: path too long\n");

}

static int rlocate_inode_create( struct inode *dir, struct dentry *dentry, 
                                 int mode )
{
        insert_path( dentry, 'a' );
        return 0;
}

static int rlocate_inode_mkdir (struct inode *dir, struct dentry * dentry, 
                                int mode)
{
  	insert_path( dentry, 'a' );
  	return 0;
}

static int rlocate_inode_link (struct dentry * old_dentry,
                               struct inode *dir, 
                               struct dentry * dentry)
{
  	insert_path( dentry, 'a' );
  	return 0;
}

static int rlocate_inode_symlink (struct inode *dir, struct dentry * dentry, 
                                  const char *old_name)
{
  	insert_path( dentry, 'a' );
  	return 0;
}

static int rlocate_inode_mknod (struct inode *dir, struct dentry * dentry,
                                int mode, dev_t dev)
{
        insert_path( dentry, 'a' );
  	return 0;
}

static int rlocate_inode_rename( struct inode * old_dir,
                                 struct dentry * old_dentry,
                                 struct inode * new_dir,
                                 struct dentry * new_dentry )
{
        if (new_dentry->d_inode)
                return 0;
        if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
                // entry is a file not a directory
                insert_path( new_dentry, 'a' );        
        } else {
                insert_path( new_dentry, 'm' );        
        }
        return 0;
}

static int rlocate_sb_mount( char *dev_name, 
			     struct nameidata *nd, 
			     char *type, 
			     unsigned long flags, 
			     void *data)
{
	RELOAD = '1';
        return 0;
}

//static int rlocate_sb_umount( struct vfsmount *mnt, int flags ) 
//{
//	RELOAD = '1';
//	return 0;
//}


#ifdef RLOCATE_UPDATES
static int rlocate_inode_permission (struct inode *inode, int mask, 
                                     struct nameidata *nd)
{
        struct dentry *dentry;
        struct list_head d_list = inode->i_dentry;
        if( ! ( ( mask & MAY_APPEND ) || (mask & MAY_WRITE) ) ) {
                return 0;
        }
        dentry = list_entry(d_list.next, struct dentry, d_alias );
  	insert_path( dentry, 'u' );
  	return 0;
}
#endif

/* module functions **********************************************************/

static int mod_register = 0;

/*
 * init_rlocate()
 */
static int __init init_rlocate(void)
{
        int ret;
	printk(KERN_INFO "rlocate version "RL_VERSION"\n");
        //init_waitqueue_head (&filenames_wq);
        // register dev 
        ret = rlocate_dev_register();
        if (ret)
                goto out;

        // register as security module
        ret = register_security( &rlocate_security_ops );
        if ( ret ) {
                ret = mod_reg_security(DEVICE_NAME, &rlocate_security_ops);
                if ( ret != 0 ) {
                        printk(KERN_ERR"Failed to register rlocate module with"
                                        " the kernel\n");
                        goto no_lsm;
                }
                mod_register = 1;
        }
        if ((path_buffer = kmalloc( PATH_MAX + 8, GFP_KERNEL )) == NULL) {
                printk (KERN_ERR "rlocate: __get_free_page failed\n");
                ret = -ENOMEM;
                goto no_path_buffer;
        }
        EXCLUDE_DIR_ARG = (char*)__get_free_page( GFP_KERNEL );
        if (!EXCLUDE_DIR_ARG) {
                ret = -ENOMEM;
                goto no_exclude_dir_arg;
        }
        STARTING_PATH_ARG = (char*)__get_free_page( GFP_KERNEL );
        if (!STARTING_PATH_ARG) {
                ret = -ENOMEM;
                goto no_starting_path_arg;
        }
        OUTPUT_ARG = (char*)__get_free_page( GFP_KERNEL );
        if (!OUTPUT_ARG) {
                ret = -ENOMEM;
                goto no_output_arg;
        }
        *EXCLUDE_DIR_ARG = '\0';
        *STARTING_PATH_ARG = '\0';
        *OUTPUT_ARG = '\0';
        // create proc entry
        ret = rlocate_init_procfs();
	if (ret) {
		printk (KERN_ERR "rlocate: rlocate_init_procfs() failed.\n");
                goto no_proc;
	}
        // set timeout for daemon inactivity
        NEXT_D_TIMEOUT = jiffies + D_TIMEOUT;
  	goto out;

no_proc:
        free_page( (unsigned long)OUTPUT_ARG);
no_output_arg:
        free_page( (unsigned long)STARTING_PATH_ARG);
no_starting_path_arg:
        free_page( (unsigned long)EXCLUDE_DIR_ARG);
no_exclude_dir_arg:
        kfree(path_buffer); 
no_path_buffer:
        if ( mod_register ) 
                mod_unreg_security(DEVICE_NAME, &rlocate_security_ops);
        else
                unregister_security(&rlocate_security_ops);
no_lsm:
        rlocate_dev_unregister();
out:
        return ret;
}

/*
 * exit_rlocate()
 */
static void __exit exit_rlocate(void)
{
        rlocate_exit_procfs();
	remove_string_list(&filenames_list);
        free_page( (unsigned long)OUTPUT_ARG);
        free_page( (unsigned long)STARTING_PATH_ARG);
        free_page( (unsigned long)EXCLUDE_DIR_ARG);
        kfree(path_buffer); 
        if ( mod_register ) {
		if (mod_unreg_security(DEVICE_NAME, &rlocate_security_ops)) 
                        printk(KERN_INFO "rlocate: failed to unregister "
					 "rlocate security module with primary "
					 "module.\n");
	} else if (unregister_security(&rlocate_security_ops)) 
                        printk(KERN_INFO "rlocate: failed to unregister "
					 "rlocate security module.\n");
        
        rlocate_dev_unregister();
  	printk(KERN_INFO "rlocate: unloaded\n");
}


security_initcall( init_rlocate );
module_exit( exit_rlocate );

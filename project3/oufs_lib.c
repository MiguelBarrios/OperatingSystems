/**
 *  Project 3
 *  oufs_lib.c
 *
 *  Author: CS3113
 *
 */

#include "oufs_lib.h"
#include "oufs_lib_support.h"
#include "virtual_disk.h"

// Yes ... a global variable
int debug = 1;

// Translate inode types to descriptive strings
const char *INODE_TYPE_NAME[] = {"UNUSED", "DIRECTORY", "FILE"};

/**
 Read the OUFS_PWD, OUFS_DISK, OUFS_PIPE_NAME_BASE environment
 variables copy their values into cwd, disk_name an pipe_name_base.  If these
 environment variables are not set, then reasonable defaults are
 given.

 @param cwd String buffer in which to place the OUFS current working directory.
 @param disk_name String buffer in which to place file name of the virtual disk.
 @param pipe_name_base String buffer in which to place the base name of the
            named pipes for communication to the server.

 PROVIDED
 */
void oufs_get_environment(char *cwd, char *disk_name,
			  char *pipe_name_base)
{
  // Current working directory for the OUFS
  char *str = getenv("OUFS_PWD");
  if(str == NULL) {
    // Provide default
    strcpy(cwd, "/");
  }else{
    // Exists
    strncpy(cwd, str, MAX_PATH_LENGTH-1);
  }

  // Virtual disk location
  str = getenv("OUFS_DISK");
  if(str == NULL) {
    // Default
    strcpy(disk_name, "vdisk1");
  }else{
    // Exists: copy
    strncpy(disk_name, str, MAX_PATH_LENGTH-1);
  }

  // Pipe name base
  str = getenv("OUFS_PIPE_NAME_BASE");
  if(str == NULL) {
    // Default
    strcpy(pipe_name_base, "pipe");
  }else{
    // Exists: copy
    strncpy(pipe_name_base, str, MAX_PATH_LENGTH-1);
  }

}

/**
 * Completely format the virtual disk (including creation of the space).
 *
 * NOTE: this function attaches to the virtual disk at the beginning and
 *  detaches after the format is complete.
 *	
 * - Zero out all blocks on the disk.					COMPLETE
 * - Initialize the master block: 
 		- mark inode 0 as allocated 					COMPLETE
 		- Initialize the linked list of free blocks     COMPLETE
 * - Initialize root directory inode 
 * - Initialize the root directory in block ROOT_DIRECTORY_BLOCK
 *
 * @return 0 if no errors
 *         -x if an error has occurred.
 *
 */

int oufs_format_disk(char  *virtual_disk_name, char *pipe_name_base)
{
  // Attach to the virtual disk
  if(virtual_disk_attach(virtual_disk_name, pipe_name_base) != 0) {
    return(-1);
  }

  BLOCK block;

  //------------ Zero out the block -----------
  memset(&block, 0, BLOCK_SIZE);
  for(int i = 0; i < N_BLOCKS; ++i) {
    if(virtual_disk_write_block(i, &block) < 0) {
      return(-2);
    }
  }

  //-------- Master Block initialization ---------

  block.next_block = UNALLOCATED_BLOCK;
  block.content.master.inode_allocated_flag[0] = 0x80;  //mark inode 0(Root dir inode) as allocated

  //Initialize the linked list of free blocks
  BLOCK_REFERENCE front = 6;  //6 becuase the root directory will already be alocated
  BLOCK_REFERENCE end = 127;
  block.content.master.unallocated_front = front; //first free block after inode blocks
  block.content.master.unallocated_end = end; //last block

  //writing master block to disk
  if(virtual_disk_write_block(MASTER_BLOCK_REFERENCE, &block) == -1){
    fprintf(stderr, "writing master block to disk error: oufs_lib -> oufs_format_disk\n");
  }

  //initializes remainder of free list
  BLOCK link;
  for(BLOCK_REFERENCE cur = 6, next = 7; cur < N_BLOCKS - 1; ++next, ++cur)
  {
      link.next_block = next;
      
      //write block to disk
      if(virtual_disk_write_block(cur, &link) == -1){
        fprintf(stderr, "write to block error: oufs_lib -> oufs_format_disk\n");
    }
  }

  //Writes Last link  in Free list to disk
  link.next_block = UNALLOCATED_BLOCK;
  if(virtual_disk_write_block(N_BLOCKS - 1, &link) == -1){
        fprintf(stderr, "write to block error\n");
  }
  //--------- End Master Block initialization --------


  //-------- Inodes initialization -------------------  
  // ?   do the inodes have to be linked to the blocks at this point
  BLOCK inodeBlock;
  inodeBlock.next_block = UNALLOCATED_BLOCK;

  INODE inode;
  inode.type = UNUSED_TYPE;
  inode.n_references = 0;
  inode.content = UNALLOCATED_BLOCK;
  inode.size = 0;

  for(int i = 0; i < N_INODES_PER_BLOCK; ++i){
  	 inodeBlock.content.inodes.inode[i] = inode;
  }

  for(BLOCK_REFERENCE i = 1; i <= N_INODE_BLOCKS; ++i){
  	if(virtual_disk_write_block(i, &inodeBlock) == -1){
        fprintf(stderr, "Writing inode block error oufs_lib -> oufs_format_disk()\n");
 	 }
  }

  //----------- init Root directory inode and block -------
  INODE rootInode;
  oufs_init_directory_structures(&rootInode, &block, ROOT_DIRECTORY_BLOCK,
				 ROOT_DIRECTORY_INODE, ROOT_DIRECTORY_INODE);

  // Write the results to the disk
    if(oufs_write_inode_by_reference(ROOT_DIRECTORY_INODE  , &rootInode) != 0) {
	    return(-3);  fprintf(stderr, "Write inode by refferenced failed: oufs_lib\n");
  }
  
  //Test
  fprintf(stderr, "Number of inodes, %d\n", N_INODES);


  // Done
  virtual_disk_detach();
 
  return(0);
}

/*
 * Compare two inodes for sorting, handling the
 *  cases where the inodes are not valid
 *
 * @param e1 Pointer to a directory entry
 * @param e2 Pointer to a directory entry
 * @return -1 if e1 comes before e2 (or if e1 is the only valid one)
 * @return  0 if equal (or if both are invalid)
 * @return  1 if e1 comes after e2 (or if e2 is the only valid one)
 *
 * Note: this function is useful for qsort()
 */
static int inode_compare_to(const void *d1, const void *d2)
{
  // Type casting from generic to DIRECTORY_ENTRY*
  DIRECTORY_ENTRY* e1 = (DIRECTORY_ENTRY*) d1;
  DIRECTORY_ENTRY* e2 = (DIRECTORY_ENTRY*) d2;


  if(e1 -> inode_reference == UNALLOCATED_INODE && e2 -> inode_reference != UNALLOCATED_INODE){
      return -1;
  }
  else if(e1 -> inode_reference != UNALLOCATED_INODE && e2 -> inode_reference == UNALLOCATED_INODE){
      return 1;
  }

  if(strcmp(e1 -> name, e2 -> name) == 0)
    return 0;
  else if(strcmp(e1 -> name, e2 -> name) < 0)
    return -1;
  else
    return 1;

}


/**
 * Print out the specified file (if it exists) or the contents of the 
 *   specified directory (if it exists)
 *
 * If a directory is listed, then the valid contents are printed in sorted order
 *   (as defined by strcmp()), one per line.  We know that a directory entry is
 *   valid if the inode_reference is not UNALLOCATED_INODE.
 *   Hint: qsort() will do to sort for you.  You just have to provide a compareTo()
 *   function (just like in Java!)
 *   Note: if an entry is a directory itself, then its name must be followed by "/"
 *
 * @param cwd Absolute path representing the current working directory
 * @param path Absolute or relative path to the file/directory
 * @return 0 if success
 *         -x if error
 *                          OUPUT MUST BE SORTED
 */

int oufs_list(char *cwd, char *path)
{
  INODE_REFERENCE parent;
  INODE_REFERENCE child;

  // Look up the inodes for the parent and child
  int ret = oufs_find_file(cwd, path, &parent, &child, NULL);

  // Did we find the specified file?
  if(ret == 0 && child != UNALLOCATED_INODE)
  {
    // Element found: read the inode
    INODE inode;
    if(oufs_read_inode_by_reference(child, &inode) != 0) 
      return(-1);

    if(debug)
      fprintf(stderr, "\tDEBUG: Child found (type=%s).\n",  INODE_TYPE_NAME[inode.type]);

    // TODO: complete implementation

    INODE node;
    if(oufs_read_inode_by_reference(child, &node) != 0){
        fprintf(stderr, "Read inode Error\n");
    }


    BLOCK block;
    if(virtual_disk_read_block(node.content, &block) != 0) {
    fprintf(stderr, "Read Error\n");
    }

    fprintf(stderr, "Num directories %d\n", inode.size);
    const char *names[inode.size];


    //TODO: ouput must be sorted
    DIRECTORY_BLOCK directory = block.content.directory;
    int count = 0;
    for(int i = 0; i < N_DIRECTORY_ENTRIES_PER_BLOCK; ++i)
    {
        if(directory.entry[i].inode_reference != UNALLOCATED_INODE)
        {
          names[count] = directory.entry[i].name;
          ++count;
        }
    }

    

    for(int i = 0; i < count; ++i){
        fprintf(stderr, "%s/\n", names[i]);
    }


  }else {
    // Did not find the specified file/directory
    fprintf(stderr, "Not found\n");
    if(debug){
      fprintf(stderr, "\tDEBUG: (%d)\n", ret);
    }
  }
  // Done: return the status from the search
  return(ret);
}




///////////////////////////////////
/**
 * Make a new directory
 *
 * To be successful:
 *  - the parent must exist and be a directory
 *  - the parent must have space for the new directory
 *  - the child must not exist
 *
 * @param cwd Absolute path representing the current working directory
 * @param path Absolute or relative path to the file/directory
 * @return 0 if success
 *         -x if error
 *
 */
int oufs_mkdir(char *cwd, char *path)
{
  INODE_REFERENCE parent;
  INODE_REFERENCE child;

  // Name of a directory within another directory
  char local_name[MAX_PATH_LENGTH];
  int ret;

  // Attempt to find the specified directory
  if((ret = oufs_find_file(cwd, path, &parent, &child, local_name)) < -1) {
    if(debug)
      fprintf(stderr, "oufs_mkdir(): ret = %d\n", ret);
    return(-1);
  };






  // TODO: complete implementation
  return (-1);  
}

/**
 * Remove a directory
 *
 * To be successul:
 *  - The directory must exist and must be empty
 *  - The directory must not be . or ..
 *  - The directory must not be /
 *
 * @param cwd Absolute path representing the current working directory
 * @param path Abslute or relative path to the file/directory
 * @return 0 if success
 *         -x if error
 *
 */
int oufs_rmdir(char *cwd, char *path)
{
  INODE_REFERENCE parent;
  INODE_REFERENCE child;
  char local_name[MAX_PATH_LENGTH];

  // Try to find the inode of the child
  if(oufs_find_file(cwd, path, &parent, &child, local_name) < -1) {
    return(-4);
  }

  // TODO: complet implementation


  // Success
  return(0);
}

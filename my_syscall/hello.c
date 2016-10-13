#include<linux/kernel.h>
#include <linux/namei.h>
#include <linux/fcntl.h>
#include <linux/syscalls.h>
#include<linux/fs.h>
#include<linux/slab.h>
#include<asm/segment.h>
#include<asm/uaccess.h>
#define MS_PG_SZ 4096
//asmlinkage long sys_hello(void){

struct hello_file{
	struct filename *kf_name; // file name in kernel space
	struct file	*f;	  // file structure
};

struct hello_file_operation{
	struct hello_file *hf;
	void 		*buff;	  // buffer to store data when read/written
	int 		buff_sz;  // This is the size of buffer 
	int 		buff_dsz; // Amount of content in the buff, should be less than buff_sz
	int 		buff_proc;// Amount of data pricessed in file or buff. For bbuff its %MS_PG_SZ 
	loff_t 		off_set;  // Offset to read from
	
};

void init_hello_file(struct hello_file *hf){
	hf->kf_name = NULL;
	hf->f = NULL;
}
void init_hello_operation(struct hello_file_operation *op){
	op->hf = NULL;
	op->buff = NULL;
	op->buff_sz = 0;
	op->buff_dsz = 0; // Since we have not even read once
	op->buff_proc = 0; 
	op->off_set = 0 ;
}
int hello_file_read(struct hello_file_operation *op){
	int res;
	struct file *f;
	mm_segment_t prev_ds;
	prev_ds = get_fs();
	set_fs(KERNEL_DS);
	//inod = op->hf->f->f_dentry->d_inode;
	f = op->hf->f;
	//res = f->f_op->read(f,op->buff,op->buff_sz,&(f->f_pos));
	res = vfs_read(f,op->buff,op->buff_sz,&(op->off_set));
	if(res >= 0){
		//op->off_set+=res;
		op->buff_proc = 0;
		op->buff_dsz = res;
		printk("file name %s,read %d bytes, offset %lld \n",op->hf->kf_name->name,res,op->off_set);
	//	printk("bytes read %d\n",res);
	}
	set_fs(prev_ds);
	return res;
}
int hello_file_write(struct hello_file_operation *op){
	int res;
	struct file *f;
	mm_segment_t prev_ds;
	prev_ds = get_fs();
	set_fs(KERNEL_DS);
	//inod = op->hf->f->f_dentry->d_inode;
	f = op->hf->f;
	//res = f->f_op->write(f,op->buff,op->buff_sz,&(f->f_pos));
h_write:
	res = vfs_write(f,op->buff+op->buff_proc,op->buff_dsz-op->buff_proc,&(op->off_set));
	if(res < 0){
		return res;
	}
	printk("File name:%s, written : %d , offset %lld\n",op->hf->kf_name->name,res,op->off_set);
	if( (op->buff_proc+res) != op->buff_dsz ){
		op->buff_proc+=res;
		goto h_write;
	}
	BUG_ON(op->buff_dsz == op->buff_proc);
	res = op->buff_dsz;
	op->buff_dsz = 0;
	op->buff_proc = 0 ; 
	set_fs(prev_ds);
	return res;
}
/* Fetches record from file buffer, stores it in buff and returns its size*/
int  get_record(struct hello_file_operation *fop, void  *rec){
	int j=0; 
	char *buff,*rec_buff;
	buff = (char *) fop->buff;
	rec_buff = (char * )rec; 
read_rec:
	/* This is the case where there is no data in file*/
	if(fop->buff_proc == fop->buff_dsz ){
		return 0;
	}
	while( (fop->buff_proc < fop->buff_dsz) && buff[fop->buff_proc]!='\n'){
		rec_buff[j] = buff[fop->buff_proc];
		fop->buff_proc++;
		j++;
	}
	/** 
	 *  Check if page content got over before record finished. 
	 *  This will happen when record spans across Page boundary. 
	 *  In this case we have to read another page of memory and
	 *  restart record reading.
	**/
	if(fop->buff_proc == fop->buff_dsz){
		printk("get_record:record across page boundries,read & restart\n");
		hello_file_read(fop);
		goto read_rec;
	}
	fop->buff_proc++; // Advancing '\n'
	/**
	 *  The record ends exactly at end of bondary. 
	 *  We just need to read a page of file memory in buffer.  
	**/
	if(fop->buff_proc == fop->buff_dsz){
		printk("get_record:record ends at PG boundry,read only \n");
		hello_file_read(fop);
	}
	return j;
}
/* stores the record to buffer in 'fop' and when buffer is full writes it to file 
 * The record should end with '\n' at its end 
 */
int put_record(struct hello_file_operation *fop,void *rec, int len){
	int i,err=0;
	char *buff,*rec_buff;
	buff = fop->buff;
	rec_buff = rec;
	i=0;
write_rec:
	while(i<len && fop->buff_dsz < fop->buff_sz){
		buff[fop->buff_dsz] = rec_buff[i];
		i++;
		fop->buff_dsz++;
	}
	if(  i<len && fop->buff_dsz == fop->buff_sz ){
		printk("Record across page boundaries, write and restart\n");
		hello_file_write(fop);
		goto write_rec;
	}
	if(fop->buff_dsz == fop->buff_sz){
		printk("Record fits exactly at boundary, write");
		hello_file_write(fop);
	}
	return err;
}
int compare(void *d1,void *d2,int d1_len,int d2_len){
	int i;
	char *b1,*b2;
	if(d1_len<d2_len){
		return -1;
	}
	else if (d1_len > d2_len){
		return 1;
	}
	else {
		b1 = (char *)d1;
		b2 = (char *)d2;
		for(i = 0 ; i < d1_len ; i++){
			if(b1[i]<b2[i])
				return -1;
			else if (b1[i]>b2[i])
				return 1;
		}	
	}
	return 0; 
}
void print_buff(void *buff_data, int len  ){
	int i ;
	char c,*buff;
	buff = (char *)buff_data;
	for(i = 0; i < len ; i++){
		c =(char) buff[i];
		printk("%c",c);
	}
	printk("\n");
}
int merge_sort(struct hello_file *in1,struct hello_file *in2,struct hello_file *out){
	struct hello_file_operation f1_op,f2_op,out_op;	
	int err = 0,val1_sz,val2_sz ;
	char *val1,*val2;
	int val1_empty, val2_empty,cmp_res;
	
	init_hello_operation(&f1_op);
	init_hello_operation(&f2_op);
	init_hello_operation(&out_op);

	f1_op.hf = in1;
	f2_op.hf = in2;
	out_op.hf = out;

	f1_op.buff = kmalloc(MS_PG_SZ,GFP_KERNEL);
	if(IS_ERR(f1_op.buff)){
		err = PTR_ERR(f1_op.buff);
		goto err;
	}
	f1_op.buff_sz = MS_PG_SZ;
	f2_op.buff = kmalloc(MS_PG_SZ,GFP_KERNEL);
	if(IS_ERR(f2_op.buff)){
		err = PTR_ERR(f2_op.buff);
		goto err_f1_buff;
	}
	f2_op.buff_sz = MS_PG_SZ;
	out_op.buff = kmalloc(MS_PG_SZ,GFP_KERNEL);
	if(IS_ERR(out_op.buff)){
		err = PTR_ERR(out_op.buff);
		goto err_f2_buff;
	}
	out_op.buff_sz = MS_PG_SZ;

	val1 = kmalloc(MS_PG_SZ,GFP_KERNEL);
	if(IS_ERR(val1)){
		err = PTR_ERR(val1);
		goto err_out_buff;
	}

	val2 = kmalloc(MS_PG_SZ,GFP_KERNEL);
	if(IS_ERR(val2)){
		err = PTR_ERR(val2);
		goto err_val1;
	}
/*Main loop for merging*/
	hello_file_read(&f1_op);
	hello_file_read(&f2_op);
	val1_empty = 1;
	val2_empty = 1;

	while(f1_op.buff_proc != f1_op.buff_dsz && 
	      f2_op.buff_proc != f2_op.buff_dsz){
		if(val1_empty)
			val1_sz = get_record(&f1_op,val1); 
		if(val2_empty)
			val2_sz = get_record(&f2_op,val2);
		
		cmp_res = compare(val1,val2,val1_sz,val2_sz);
		if( cmp_res  < 0 ){
			// First value is small , second value is greater
			//print_buff(val1,val1_sz);
			val1[val1_sz]='\n';
			val1_sz++;
			put_record(&out_op,val1,val1_sz);
			val2_empty = 0 ; 
			val1_empty = 1 ; 
		}
		else if (cmp_res > 0 ){
			// First value is greater , seconf value is smaller 
			//print_buff(val2,val2_sz); 
			val2[val2_sz]='\n';
			val2_sz++;
			put_record(&out_op,val2,val2_sz);
			val1_empty = 0 ; 
			val2_empty = 1 ; 
		}
		else{
			// Both are same 
			//print_buff(val2,val2_sz); 
			val2[val2_sz]='\n';
			val2_sz++;
			put_record(&out_op,val2,val2_sz);
			val1_empty = 0 ; 
			val2_empty = 1 ; 
		}
	
	}

/* End of main loop */	
	kfree(val2);
err_val1:
	kfree(val1);
err_out_buff:
	kfree(out_op.buff);
err_f2_buff: 
	kfree(f2_op.buff);
err_f1_buff: 
	kfree(f1_op.buff);
err:	
	return err;
}
SYSCALL_DEFINE5(hello,char*,f1,char*,f2,char*,res,unsigned int,flags,unsigned int*,data)
{
	//struct open_flags op;
	long err=0;//,ret=0;//i;//= build_open_flags(O_RDONLY,0, &op);
	//struct filename *kfname_1,*kfname_2,*kfname_res;
	//struct file *f_1,*f_2,*f_res;
	struct hello_file in1,in2,sol;
	//struct hello_file_operation op;
	//char *d;
	

	init_hello_file(&in1);
	init_hello_file(&in2);
	init_hello_file(&sol);


	in1.kf_name = getname(f1);
	if (IS_ERR(in1.kf_name)){
		err=PTR_ERR(in1.kf_name);
		goto err;
	}
	in2.kf_name = getname(f2);
	if (IS_ERR(in2.kf_name)){
		err=PTR_ERR(in2.kf_name);
		goto err_k1;
	}
	sol.kf_name = getname(res);
	if (IS_ERR(sol.kf_name)){
		err=PTR_ERR(sol.kf_name);
		goto err_k2;
	}

	//filp_open(filename, flags, mode);
	//file_open_name(k_filename,flags,mode)
	//do_filp_open(AT_FDCWD, name, &op);


	/*Opening first file */
	in1.f=file_open_name(in1.kf_name,O_RDONLY,0);
	if (IS_ERR(in1.f)) {
		//kfree(acct);
		err=PTR_ERR(in1.f);
		goto err_ksol;
	}

	if (!S_ISREG(file_inode(in1.f)->i_mode)) {
		err=-EACCES;
		goto err_f1;
	}


	/*Opening second file */
	in2.f=file_open_name(in2.kf_name,O_RDONLY,0); 
	if (IS_ERR(in2.f)) {
		err=PTR_ERR(in2.f);
		goto err_f1;
	}

	if (!S_ISREG(file_inode(in2.f)->i_mode)) {
		err=-EACCES;
		goto err_f2;
	}
	/*Opening and creating result file */	
	sol.f=file_open_name(sol.kf_name,O_WRONLY|O_CREAT,S_IRWXU); 
	if (IS_ERR(sol.f)) {
		err=PTR_ERR(sol.f);
		goto err_f2;
	}

	if (!S_ISREG(file_inode(sol.f)->i_mode)) {
		err=-EACCES;
		goto err_f_sol;
	}

	/* Both files are available for input */
/*
	init_hello_operation(&op);
	op.hf = &in1;
	op.buff_sz = 4096;
	op.buff = kmalloc(op.buff_sz,GFP_KERNEL);	
	
	ret =  hello_file_read(&op);
	if(ret>0){
		for(i=0;i<ret;i++){
			d = (char*)op.buff;
			printk("%c",d[i]);
		}
	}
	printk("\n");
*/
	merge_sort(&in1,&in2,&sol);
	/* End of processing the core functionality*/

err_f_sol:
	filp_close(sol.f,NULL);
err_f2:
	filp_close(in2.f,NULL);
err_f1:
	filp_close(in1.f,NULL);
err_ksol:
	putname(sol.kf_name);
err_k2:
	putname(in2.kf_name);
err_k1:
	putname(in1.kf_name);
err:
	return err;
}

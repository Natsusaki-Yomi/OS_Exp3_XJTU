#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <cstring>
#include <ctime>
#include <cmath>
#include <vector>
#include <algorithm>
#include <conio.h>
using namespace std;

typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

#define EXT2_NAME_LEN 26

struct ext2_group_desc //组描述符大小64字节
{
	char bg_volume_name[16]; //卷名
	__u16 bg_block_bitmap; //保存块位图的块号
	__u16 bg_inode_bitmap; //保存索引结点位图的块号
	__u16 bg_inode_table; //索引结点表的起始块号
	__u16 bg_free_blocks_count; //本组空闲块的个数
	__u16 bg_free_inodes_count; //本组空闲索引结点的个数
	__u16 bg_used_dirs_count; //本组目录的个数
	char password[17]; //密码
	char bg_pad[18]; //填充1
};

struct ext2_inode //索引节点，大小64字节
{
	__u16 i_mode; //文件类型及访问权限
	__u16 i_blocks; //文件的数据块个数
	__u32 i_size; //大小(字节)
	time_t i_atime; //访问时间
	time_t i_ctime; //创建时间
	time_t i_mtime; //修改时间
	time_t i_dtime; //删除时间
	__u16 i_block[8]; //指向数据块的指针
	char i_pad[8]; //填充1
};

struct ext2_dir_entry //目录项，大小32字节
{
	__u16 inode; //索引节点号
	__u16 rec_len; //目录项长度
	__u8 name_len; //文件名长度
	__u8 file_type; //文件类型(1:普通文件，2:目录…)
	char name[EXT2_NAME_LEN]; //文件名，最长26字符
};

struct bitmap //位图
{
	__u8 map_8[512];
};

struct i_block_one //多级索引中存放数据块指针的块
{
	__u16 inode[256];
};

__u16 fopen_table[16]; /*文件打开表，最多可以同时打开16个文件*/
ext2_inode open_inode[16];
__u16 last_alloc_inode; /*上次分配的索引结点号*/
__u16 last_alloc_block; /*上次分配的数据块号*/
char current_path[256]; /*当前路径(字符串)*/
ext2_group_desc group_desc; /*组描述符*/
__u16 current_dir;  /*当前目录（索引结点）*/
ext2_inode current_inode; /*当前索引节点*/
FILE* FS; 

vector<string> split(string& s, string& d) //字符串分割函数
{
	if (s == "")
	{
		return {};
	}
	char* ss = new char[s.size() + 1];
	strcpy(ss, s.c_str());
	char* dd = new char[d.size() + 1];
	strcpy(dd, d.c_str());
	char* p = strtok(ss, dd);
	vector<string> res;
	while (p)
	{
		res.push_back(p);
		p = strtok(NULL, dd);
	}
	delete[]ss;
	delete[]dd;
	return res;
}

int vector_find(vector<string>& l, string& s)
{
	int i;
	for (i = 0; i < l.size(); ++i)
	{
		if (l[i] == s)
		{
			break;
		}
	}
	return i;
}

bool is_num(string& s)
{
	for (auto& ch : s)
	{
		if (ch < '0' || ch>'9')
		{
			return false;
		}
	}
	return true;
}

string i_mode_str(__u16 i_mode) //将0000000200000111转为drwxrwxrwx
{
	string res, acs;
	if ((i_mode >> 8) == 1)
	{
		res.push_back('-');
	}
	else
	{
		res.push_back('d');
	}
	if (i_mode & 0b100)
	{
		acs.push_back('r');
	}
	else
	{
		acs.push_back('-');
	}
	if (i_mode & 0b10)
	{
		acs.push_back('w');
	}
	else
	{
		acs.push_back('-');
	}
	if (i_mode & 0b1)
	{
		acs.push_back('x');
	}
	else
	{
		acs.push_back('-');
	}
	res += acs;
	res += acs;
	res += acs;
	return res;
}

void show_group()
{
	cout << "\n空闲数据块：";
	cout << group_desc.bg_free_blocks_count;
	cout << "\n空闲索引块：";
	cout << group_desc.bg_free_inodes_count;
	cout << "\n目录个数：";
	cout << group_desc.bg_used_dirs_count;
	cout << "\n密码：";
	cout << group_desc.password << endl;
}

string get_pw()
{
	string res = "";
	char ch;
	while (1)
	{
		ch = _getch();
		if (ch == '\r')
		{
			break;
		}
		if (ch == 8)
		{
			if (res.size())
			{
				res.pop_back();
			}
		}
		else
		{
			res.push_back(ch);
		}
	}
	return res;
}

bool update_group_desc() //将内存中的组描述符更新到"硬盘"
{
	FILE* tmp = FS;
	fseek(tmp, 0, SEEK_SET);
	fwrite(&group_desc, sizeof(ext2_group_desc), 1, tmp);
	return true;
}

bool reload_group_desc() //载入可能已更新的组描述符
{
	FILE* tmp = FS;
	fseek(tmp, 0, SEEK_SET);
	fread(&group_desc, sizeof(ext2_group_desc), 1, tmp);
	return true;
}

bool load_inode_entry(ext2_inode* obj, __u16 inode_num)  //载入特定的索引结点，不存在则返回false
{
	--inode_num; //变为从0开始编号
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if ((bm.map_8[inode_num / 8] >> (7 - inode_num % 8)) & 1) //索引节点不存在
	{
		return false;
	}
	fseek(tmp, 3 * 512 + 64 * inode_num, SEEK_SET);
	fread(obj, 64, 1, tmp);
	return true;
}

bool update_inode_entry(ext2_inode* obj, __u16 inode_num) //更新特定的索引结点
{
	--inode_num; //变为从0开始编号
	if (inode_num > 4095)
	{
		return false;
	}
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if ((bm.map_8[inode_num / 8] >> (7 - inode_num % 8)) & 1) //索引节点不存在
	{
		return false;
	}
	else //索引节点已存在，更新
	{		
		fseek(tmp, 3 * 512 + 64 * inode_num, SEEK_SET);
		fwrite(obj, 64, 1, tmp);
	}
	return true;
}

__u16 ext2_new_inode() //分配一个新的索引结点，成功返回索引节点号，失败返回65535
{
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	int i, j;
	for (i = 0; i < 512; ++i)
	{
		if (bm.map_8[i] != 0)
		{
			for (j = 0; j < 8; ++j)
			{
				if (((bm.map_8[i] << j) & 0b10000000) != 0) //bitmap第j位为1
				{
					break;
				}
			}
			bm.map_8[i] = bm.map_8[i] & int(0xff - pow(2, 7 - j));
			fseek(tmp, 2 * 512, SEEK_SET);
			fwrite(&bm, 512, 1, tmp);
			last_alloc_inode = 8 * i + j + 1;
			--group_desc.bg_free_inodes_count;
			update_group_desc();
			return 8 * i + j + 1;
		}
	}
	return 65535;
}

__u16 ext2_alloc_block() //分配一个新的数据块，成功返回数据块号，失败返回65535
{
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	int i, j;
	for (i = 0; i < 512; ++i)
	{
		if (bm.map_8[i] != 0)
		{
			for (j = 0; j < 8; ++j)
			{
				if (((bm.map_8[i] << j) & 0b10000000) != 0) //bitmap第j位为1
				{
					break;
				}
			}
			bm.map_8[i] = bm.map_8[i] & int(0xff - pow(2, 7 - j));
			fseek(tmp, 512, SEEK_SET);
			fwrite(&bm, 512, 1, tmp);
			last_alloc_block = 8 * i + j;
			--group_desc.bg_free_blocks_count;
			update_group_desc();
			return 8 * i + j;
		}
	}
	return 65535;
}

bool ext2_free_inode(__u16 inode_num) //释放特定的索引结点
{
	if (inode_num == 65535)
	{
		return false;
	}
	--inode_num; //变为从0开始编号
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if ((bm.map_8[inode_num / 8] & (int(pow(2, 7 - inode_num % 8)))) == 0)
	{
		++group_desc.bg_free_inodes_count;
	}
	bm.map_8[inode_num / 8] = bm.map_8[inode_num / 8] | (int(pow(2, 7 - inode_num % 8)));
	fseek(tmp, 2 * 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp);
	update_group_desc();
	return true;
}

bool ext2_free_block_bitmap(__u16 block_num) //释放特定块号的数据块位图
{
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if((bm.map_8[block_num / 8] & (int(pow(2, 7 - block_num % 8)))) == 0)
	{
		++group_desc.bg_free_blocks_count;
	}
	bm.map_8[block_num / 8] = bm.map_8[block_num / 8] | (int(pow(2, 7 - block_num % 8)));
	fseek(tmp, 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp);
	update_group_desc();
	return true;
}

bool ext2_free_blocks(ext2_inode* obj) //释放特定文件的所有数据块
{
	int i, j;
	if (obj->i_blocks <= 6) //直接索引
	{
		for (i = 0; i < obj->i_blocks; ++i) //删直接索引
		{
			ext2_free_block_bitmap(obj->i_block[i]);
		}
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks <= 6 + 256) //一级索引
		{
			for (i = 0; i < 6; ++i) //删直接索引
			{
				ext2_free_block_bitmap(obj->i_block[i]);
			}
			i_block_one one;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (i = 0; i < obj->i_blocks - 6; ++i) //删一级索引
			{
				ext2_free_block_bitmap(one.inode[i]);
			}
			ext2_free_block_bitmap(obj->i_block[6]);
		}
		else //二级索引
		{
			for (i = 0; i < 6; ++i) //删直接索引
			{
				ext2_free_block_bitmap(obj->i_block[i]);
			}
			i_block_one one, two;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (i = 0; i < 256; ++i) //删一级索引
			{
				ext2_free_block_bitmap(one.inode[i]);
			}
			ext2_free_block_bitmap(obj->i_block[6]);
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			for (i = j = 0; 256 + 6 + i * 256 < obj->i_blocks; ++i) //删二级索引
			{
				fseek(tmp, 515 * 512 + 512 * two.inode[i], SEEK_SET);
				fread(&one, 512, 1, tmp);
				for (j = 0; 256 + 6 + i * 256 + j < obj->i_blocks && j < 256; ++j)
				{
					ext2_free_block_bitmap(one.inode[j]);
				}
				ext2_free_block_bitmap(two.inode[i]);
			}
			ext2_free_block_bitmap(obj->i_block[7]);
		}
	}
	obj->i_blocks = obj->i_size = 0;
	time_t now;
	time(&now);
	obj->i_dtime = obj->i_mtime = obj->i_atime = now;
	return true;
}

bool add_a_block(ext2_inode* obj, __u16 inode_num) //为文件增加一个数据块
{
	if (group_desc.bg_free_blocks_count == 0)
	{
		return false;
	}
	if (group_desc.bg_free_blocks_count == 1 && obj->i_blocks % 256 == 6) //还需要增加一个一级索引块
	{
		return false;
	}
	if (group_desc.bg_free_blocks_count == 2 && obj->i_blocks == 256 + 6) //还需要增加一个二级索引块
	{
		return false;
	}
	__u16 block_num = ext2_alloc_block();
	if (obj->i_blocks < 6) //直接索引
	{
		obj->i_block[obj->i_blocks] = block_num;
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks < 6 + 256) //一级索引
		{
			i_block_one one;
			if (obj->i_blocks == 6) //新建一级索引
			{
				__u16 block_num_one = ext2_alloc_block();
				obj->i_block[6] = block_num_one;
			}
			else
			{
				fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
				fread(&one, 512, 1, tmp);
			}
			one.inode[obj->i_blocks - 6] = block_num;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fwrite(&one, 512, 1, tmp);			
		}
		else //二级索引
		{
			i_block_one one, two;
			if (obj->i_blocks == 256 + 6) //新建二级索引
			{
				__u16 block_num_two = ext2_alloc_block();
				obj->i_block[7] = block_num_two;
			}
			else
			{
				fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
				fread(&two, 512, 1, tmp);
			}
			if (obj->i_blocks % 256 == 6) //新建一级索引
			{
				__u16 block_num_one = ext2_alloc_block();
				two.inode[(obj->i_blocks - 6 - 256) / 256] = block_num_one;
				fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
				fwrite(&two, 512, 1, tmp);
			}
			else
			{
				fseek(tmp, 515 * 512 + 512 * two.inode[(obj->i_blocks - 6 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, tmp);
			}
			one.inode[(obj->i_blocks - 6) % 256] = block_num;
			fseek(tmp, 515 * 512 + 512 * two.inode[(obj->i_blocks - 6 - 256) / 256], SEEK_SET);
			fwrite(&one, 512, 1, tmp);
		}
	}
	++obj->i_blocks;
	update_inode_entry(obj, inode_num);
	load_inode_entry(&current_inode, current_dir);
	return true;
}

bool delete_a_block(ext2_inode* obj, __u16 inode_num) //删除最后一个数据块
{
	if (obj->i_blocks <= 6) //直接索引
	{
		ext2_free_block_bitmap(obj->i_block[obj->i_blocks - 1]);
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks <= 6 + 256) //一级索引
		{
			i_block_one one;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			ext2_free_block_bitmap(one.inode[obj->i_blocks - 7]);
			if (obj->i_blocks == 7) //一并删除一级索引块
			{
				ext2_free_block_bitmap(obj->i_block[6]);
			}
		}
		else //二级索引
		{
			i_block_one one, two;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			fseek(tmp, 515 * 512 + 512 * (two.inode[(obj->i_blocks - 256 - 7) / 256]), SEEK_SET);
			fread(&one, 512, 1, tmp);
			ext2_free_block_bitmap(one.inode[(obj->i_blocks - 7) % 256]);
			if (obj->i_blocks % 256 == 7) //一并删除一级索引块
			{
				ext2_free_block_bitmap(two.inode[(obj->i_blocks - 256 - 7) / 256]);
			}
			if (obj->i_blocks == 256 + 7) //一并删除二级索引块
			{
				ext2_free_block_bitmap(obj->i_block[7]);
			}
		}
	}
	--obj->i_blocks;
	update_inode_entry(obj, inode_num);
	return true;
}

vector<ext2_dir_entry> load_all_dir(ext2_inode* obj) //获取目录的所有目录项
{
	FILE* tmp = FS;
	vector<ext2_dir_entry> dir_en(obj->i_size / 32); //共有obj->i_size/32个目录项
	int i, j, k, cnt = 0;
	if (obj->i_blocks <= 6) //前六块可以通过直接索引找到
	{
		for (i = 0; i < obj->i_blocks; ++i)
		{
			for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
			{
				fseek(tmp, 515 * 512 + 512 * obj->i_block[i] + 32 * j, SEEK_SET);
				fread(&dir_en[cnt], 32, 1, tmp);
			}
		}
	}
	else
	{
		if (obj->i_blocks <= 6 + 128) //一级索引
		{
			for (i = 0; i < 6; ++i) //前六块可以通过直接索引找到
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * obj->i_block[i] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
			i_block_one one;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (; i < obj->i_blocks; ++i) //之后的块用一级索引
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * one.inode[i - 6] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
		}
		else //二级索引
		{
			for (i = 0; i < 6; ++i) //前六块可以通过直接索引找到
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * obj->i_block[i] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
			i_block_one one, two;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (; i < 256 + 6; ++i) //之后的256块用一级索引
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * one.inode[i - 6] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			for (j = 0; 256 + 6 + 256 * j < obj->i_blocks; ++j) //之后的块用二级索引
			{
				fseek(tmp, 515 * 512 + 512 * two.inode[j], SEEK_SET);
				fread(&one, 512, 1, tmp);
				for (; i < obj->i_blocks; ++i)
				{
					for (k = 0; k < 16 && cnt < dir_en.size(); ++k, ++cnt)
					{
						fseek(tmp, 515 * 512 + 512 * one.inode[(i - 6) % 256] + 32 * k, SEEK_SET);
						fread(&dir_en[cnt], 32, 1, tmp);
					}
				}
			}
		}
	}
	return dir_en;
}

__u16 search_filename(string& filename, __u8& type) //查找文件，返回文件对应的索引节点号，文件不存在则返回65535
{
	load_inode_entry(&current_inode, current_dir);
	if(!filename.size())
	{
		return 65535;
	}
	if (filename == "/")
	{
		type = 2;
		return 1;
	}
	string d = "/";
	vector<string> sp = split(filename, d);
	if (!sp.size())
	{
		return 65535;
	}
	ext2_inode tmp;
	vector<ext2_dir_entry> dir_en;
	int i, j;
	if (filename[0] == '/')
	{
		load_inode_entry(&tmp, 1); //tmp = root
	}
	else
	{
		tmp = current_inode;
	}
	for (i = 0; i < sp.size(); ++i)
	{
		dir_en = load_all_dir(&tmp);
		for (j = 0; j < dir_en.size(); ++j)
		{
			if (!strcmp(dir_en[j].name, sp[i].c_str()))
			{
				type = dir_en[j].file_type;
				break;
			}
		}
		if (j == dir_en.size() || i != sp.size() - 1 && dir_en[j].file_type == 1)
		{
			return 65535;
		}
		if (i != sp.size() - 1)
		{
			load_inode_entry(&tmp, dir_en[j].inode);
		}		
	}
	return dir_en[j].inode;
}

bool is_open(__u16 inode_num)
{
	int i;
	for (i = 0; i < 16; ++i)
	{
		if (fopen_table[i] == inode_num)
		{
			return true;
		}
	}
	return false;
}

void initialize_memory()
{
	int i;
	for (i = 0; i < 16; ++i) //文件打开表清空
	{
		fopen_table[i] = 65535;
	}
	last_alloc_inode = 1; //上次分配了1号索引节点（根目录）
	last_alloc_block = 0; //上次分配了0号数据块（根目录）
	current_dir = 1; //当前打开了1号索引节点（根目录）
	strcpy(current_path, (char*)"/");
	strcpy(group_desc.bg_volume_name, (char*)"ylx");
	strcpy(group_desc.password, (char*)"admin");
	group_desc.bg_block_bitmap = 1;
	group_desc.bg_inode_bitmap = 2;
	group_desc.bg_inode_table = 3;
	group_desc.bg_free_inodes_count = 4095;
	group_desc.bg_free_blocks_count = 4095;
	group_desc.bg_used_dirs_count = 1;
	for (i = 0; i < 18; ++i) //填充0xFF
	{
		group_desc.bg_pad[i] = 0b1111111;
	}
}

void initialize_disk() //创建文件、创建根目录
{
	int i;
	while (!FS)
	{
		FS = fopen("FS", "wb+");
	}
	bitmap bm;
	for (i = 0; i < 512; ++i)
	{
		bm.map_8[i] = 255;
	}
	FILE* tmp = FS;
	for (i = 0; i < 3 + 512 + 4096; ++i)
	{
		fwrite(&bm, 512, 1, tmp);
	}
	update_group_desc();
	bm.map_8[0] = 0b01111111; //两位图第一位均设置为0
	fseek(tmp, 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp); //写数据块位图
	fseek(tmp, 2 * 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp); //写索引节点位图
	time_t now; //当前时间
	time(&now);
	ext2_dir_entry root_dir_1, root_dir_2; //根目录下的当前目录和父目录
	root_dir_1.file_type = root_dir_2.file_type = 2;
	strcpy(root_dir_1.name, ".");
	strcpy(root_dir_2.name, "..");
	root_dir_1.name_len = 1;
	root_dir_2.name_len = 2;
	root_dir_1.rec_len = root_dir_2.rec_len = sizeof(ext2_dir_entry); //大小固定为32字节
	root_dir_1.inode = root_dir_2.inode = 1; //根目录的索引节点号为1
	fseek(tmp, 515 * 512, SEEK_SET);
	fwrite(&root_dir_1, sizeof(root_dir_1), 1, tmp);
	fseek(tmp, 515 * 512 + sizeof(root_dir_1), SEEK_SET);
	fwrite(&root_dir_2, sizeof(root_dir_2), 1, tmp);
	ext2_inode root; //创建根目录
	root.i_atime = root.i_ctime = root.i_mtime = now;
	root.i_dtime = 0; //设置时间信息
	root.i_mode = 0x0206; //drw-rw-rw-
	root.i_size = 2 * sizeof(ext2_dir_entry); //两个目录项，数据大小为64字节
	root.i_blocks = 1;
	root.i_block[0] = 0;
	for (i = 1; i < 8; ++i)
	{
		root.i_block[i] = 65535;
	}
	for (i = 0; i < 8; ++i)
	{
		root.i_pad[i] = 0b1111111;
	}
	fseek(tmp, 3 * 512, SEEK_SET);
	fwrite(&root, sizeof(root), 1, tmp);
	current_inode = root;
}

void update_memory()
{
	int i;
	for (i = 0; i < 16; ++i) //文件打开表清空
	{
		fopen_table[i] = 65535;
	}
	last_alloc_inode = 1; //上次分配了1号索引节点（根目录）
	last_alloc_block = 0; //上次分配了0号数据块（根目录）
	//以上两条无实际作用
	current_dir = 1;
	strcpy(current_path, (char*)"/");
	reload_group_desc();
	load_inode_entry(&current_inode, 1);
}

void dir(ext2_inode& dir_inode, __u16 dir_num, bool l, bool a, bool s, bool r) //ls与dir等效，均为列出dir_inode包含的文件。l：详细信息，a：隐藏文件，s：按大小排序，r：逆序
{
	FILE* tmp = FS;
	int i;
	vector<vector<string>> res; //res每一项为{权限，文件名，大小，创建时间，访问时间，修改时间}
	time_t now;
	time(&now);
	dir_inode.i_atime = now;
	update_inode_entry(&dir_inode, dir_num);
	vector<ext2_dir_entry> dir_en = load_all_dir(&dir_inode); //所有目录项
	if (!l)
	{
		for (auto& item : dir_en)
		{
			if (item.name[0] == '.') //隐藏文件
			{
				if (a)
				{
					res.push_back({"", item.name, "", "", "", ""});
				}
			}
			else
			{
				res.push_back({ "", item.name, "", "", "", "" });
			}
		}
		sort(res.begin(), res.end(), [&r](const vector<string>& a, const vector<string>& b)
			{
				if (r)
				{
					return a[1] > b[1];
				}
				else
				{
					return a[1] < b[1];
				}
			});
		for (i = 0; i < res.size(); ++i)
		{
			cout << res[i][1];
			if (i != res.size() - 1)
			{
				cout << ' ';
			}
		}
	}
	else
	{
		ext2_inode tmp;
		for (auto& item : dir_en)
		{			
			if (item.name[0] == '.' && a || item.name[0] != '.') //隐藏文件
			{
				load_inode_entry(&tmp, item.inode);
				char ct[50], at[50], mt[50];
				strcpy(ct, ctime(&tmp.i_ctime));
				ct[strlen(ct) - 1] = '\0';
				strcpy(at, ctime(&tmp.i_atime));
				at[strlen(at) - 1] = '\0';
				strcpy(mt, ctime(&tmp.i_mtime));
				mt[strlen(mt) - 1] = '\0';
				res.push_back({ i_mode_str(tmp.i_mode), item.name, to_string(tmp.i_size), ct, at, mt });
			}
		}
		sort(res.begin(), res.end(), [&s, &r](const vector<string>& a, const vector<string>& b)
			{
				if (r)
				{
					if (s)
					{
						return stoi(a[2]) > stoi(b[2]);
					}
					return a[1] > b[1];
				}
				else
				{
					if (s)
					{
						return stoi(a[2]) < stoi(b[2]);
					}
					return a[1] < b[1];
				}
			});
		cout << setw(12) << left << "类型/权限" << setw(28) << left << "文件名" << setw(16) << left << "文件大小" << setw(32) << left << "创建时间" << setw(32) << left << "访问时间" << setw(32) << left << "修改时间" << endl;
		for (i = 0; i < res.size(); ++i)
		{
			cout << setw(12) << left << res[i][0] << setw(28) << left << res[i][1] << setw(16) << left << res[i][2] << setw(32) << left << res[i][3] << setw(32) << left << res[i][4] << setw(32) << left << res[i][5] << endl;
		}
	}
	load_inode_entry(&current_inode, current_dir);
}

void attrib(ext2_inode& attrib_inode, string& filename, bool l) //l: 详细信息
{
	if (!l)
	{
		cout << filename << endl;
		return;
	}
	char ct[50], at[50], mt[50];
	strcpy(ct, ctime(&attrib_inode.i_ctime));
	ct[strlen(ct) - 1] = '\0';
	strcpy(at, ctime(&attrib_inode.i_atime));
	at[strlen(at) - 1] = '\0';
	strcpy(mt, ctime(&attrib_inode.i_mtime));
	mt[strlen(mt) - 1] = '\0';
	cout << setw(12) << left << "类型/权限" << setw(28) << left << "文件名" << setw(16) << left << "文件大小" << setw(32) << left << "创建时间" << setw(32) << left << "访问时间" << setw(32) << left << "修改时间" << endl;
	cout << setw(12) << left << i_mode_str(attrib_inode.i_mode) << setw(28) << left << filename << setw(16) << left << to_string(attrib_inode.i_size) << setw(32) << left << ct << setw(32) << left << at << setw(32) << left << mt << endl;
	return;
}

void cd(string& filename) //切换当前目录
{
	load_inode_entry(&current_inode, current_dir);
	__u8 type;
	__u16 inode = search_filename(filename, type);
	time_t now;
	time(&now);
	if (inode == 65535)
	{
		cout << "不存在名为" << filename << "的文件或目录。\n";
		return;
	}
	if (type == 1)
	{
		cout << filename << "不是一个目录。\n";
		return;
	}
	if (inode == current_dir) //返回当前目录
	{
		current_inode.i_atime = now;
		update_inode_entry(&current_inode, inode);
		return;
	}
	if (filename[0] == '/')
	{
		if (filename.size() > 255)
		{
			cout << "路径名过长。\n";
			return;
		}
		strcpy(current_path, filename.c_str());
	}
	else
	{
		int l = strlen(current_path) - 1;
		if (!l)
		{
			if (strlen(current_path) + filename.size() > 255)
			{
				cout << "路径名过长。\n";
				return;
			}
			strcat(current_path, filename.c_str());
		}
		else
		{
			if (filename == "..")
			{
				while (l >= 0 && current_path[l] != '/')
				{
					--l;
				}
				current_path[max(l, 1)] = '\0';
			}
			else
			{
				if (strlen(current_path) + filename.size() + 1 > 255)
				{
					cout << "路径名过长。\n";
					return;
				}
				strcat(current_path, "/");
				strcat(current_path, filename.c_str());
			}
		}
	}
	current_dir = inode;
	load_inode_entry(&current_inode, current_dir);
	current_inode.i_atime = now;
	update_inode_entry(&current_inode, inode);
	load_inode_entry(&current_inode, current_dir);
}

void create(string& filename, __u8 type) //create(文件名，文件类型)
{
	load_inode_entry(&current_inode, current_dir);
	string d = "/";
	vector<string> sp = split(filename, d);
	if (!sp.size())
	{
		cout << "文件名不符合规范。\n";
		return;
	}
	if (sp[sp.size() - 1].size() > 25)
	{
		cout << "文件名长度最大为25。\n";
		return;
	}
	ext2_inode tmp;
	__u16 tmp_num = current_dir;
	vector<ext2_dir_entry> dir_en;
	int i, j;
	if (filename[0] == '/')
	{
		load_inode_entry(&tmp, 1); //tmp = root
		tmp_num = 1;
	}
	else
	{
		tmp = current_inode;
	}
	for (i = 0; i < sp.size(); ++i)
	{
		dir_en = load_all_dir(&tmp);
		for (j = 0; j < dir_en.size(); ++j)
		{
			if (!strcmp(dir_en[j].name, sp[i].c_str()))
			{
				break;
			}
		}
		if (i != sp.size() - 1 && (j == dir_en.size() || dir_en[j].file_type == 1))
		{
			cout << "路径无效\n";
			return;
		}
		if (i != sp.size() - 1)
		{
			load_inode_entry(&tmp, dir_en[j].inode);
			tmp_num = dir_en[j].inode;
		}
	}
	if (j != dir_en.size())
	{
		cout << "文件已存在。\n";
		return;
	}
	if (group_desc.bg_free_inodes_count == 0)
	{
		cout << "空间已满，无法创建文件。\n";
		return;
	}
	__u16 inode_num = ext2_new_inode();
	ext2_inode ct;
	time_t now;
	time(&now);
	if (type == 1) //文件
	{
		if (tmp.i_size % 512 == 0)
		{
			if (!add_a_block(&tmp, tmp_num))
			{
				cout << "空间已满，无法创建文件。\n";
				ext2_free_inode(inode_num);
				return;
			}
		}
		ct.i_atime = ct.i_ctime = ct.i_mtime = now;
		ct.i_dtime = 0;
		ct.i_blocks = 0;
		if (!strstr(sp[sp.size() - 1].c_str(), ".") || sp[sp.size() - 1].size() > 4 && (sp[sp.size() - 1].substr(sp[sp.size() - 1].size() - 4, 4) == ".exe" || sp[sp.size() - 1].substr(sp[sp.size() - 1].size() - 4, 4) == ".bin" || sp[sp.size() - 1].substr(sp[sp.size() - 1].size() - 4, 4) == ".com"))
		{
			ct.i_mode = 0x0107;
		}
		else
		{
			ct.i_mode = 0x0106;
		}
		for (i = 0; i < 8; ++i)
		{
			ct.i_pad[i] = 127;
			ct.i_block[i] = 65535;
		}
		ct.i_size = 0;
		update_inode_entry(&ct, inode_num);
		tmp.i_atime = tmp.i_mtime = now;
		update_inode_entry(&tmp, tmp_num);
		ext2_dir_entry new_dir;
		new_dir.file_type = 1;
		new_dir.inode = inode_num;
		strcpy(new_dir.name, sp[sp.size() - 1].c_str());
		new_dir.name_len = sp[sp.size() - 1].size();
		new_dir.rec_len = 32;
		FILE* temp = FS;
		if (tmp.i_blocks <= 6)
		{
			fseek(temp, 515 * 512 + 512 * tmp.i_block[tmp.i_blocks - 1] + tmp.i_size % 512, SEEK_SET);
			fwrite(&new_dir, 32, 1, temp);
		}
		else
		{
			if (tmp.i_blocks <= 6 + 256)
			{
				i_block_one one;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[6], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[tmp.i_blocks - 7] + tmp.i_size % 512, SEEK_SET);
				fwrite(&new_dir, 32, 1, temp);
			}
			else
			{
				i_block_one one, two;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[7], SEEK_SET);
				fread(&two, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * two.inode[(tmp.i_blocks - 7 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[(tmp.i_blocks - 7) % 256] + tmp.i_size % 512, SEEK_SET);
				fwrite(&new_dir, 32, 1, temp);
			}
		}
		tmp.i_size += 32;
		update_inode_entry(&tmp, tmp_num);
	}
	else //目录
	{
		if (group_desc.bg_free_blocks_count == 0)
		{
			cout << "空间已满，无法创建目录。\n";
			ext2_free_inode(inode_num);
			return;
		}
		if (tmp.i_size % 512 == 0)
		{
			if (!add_a_block(&tmp, tmp_num))
			{
				cout << "空间已满，无法创建目录。\n";
				ext2_free_inode(inode_num);
				return;
			}
		}
		FILE* temp = FS;
		__u16 block_num = ext2_alloc_block();
		ct.i_atime = ct.i_ctime = ct.i_mtime = now;
		ct.i_dtime = 0;
		ct.i_blocks = 1;
		ct.i_mode = 0x0206;
		for (i = 0; i < 8; ++i)
		{
			ct.i_pad[i] = 127;
			ct.i_block[i] = 65535;
		}
		ct.i_block[0] = block_num;
		ct.i_size = 64;
		update_inode_entry(&ct, inode_num);
		tmp.i_atime = tmp.i_mtime = now;
		update_inode_entry(&tmp, tmp_num);
		ext2_dir_entry new_dir_1, new_dir_2; //新目录下的当前目录和父目录
		new_dir_1.file_type = new_dir_2.file_type = 2;
		strcpy(new_dir_1.name, ".");
		strcpy(new_dir_2.name, "..");
		new_dir_1.name_len = 1;
		new_dir_2.name_len = 2;
		new_dir_1.rec_len = new_dir_2.rec_len = 32; //大小固定为32字节
		new_dir_1.inode = inode_num;
		new_dir_2.inode = tmp_num;
		fseek(temp, 515 * 512 + 512 * block_num, SEEK_SET);
		fwrite(&new_dir_1, 32, 1, temp);
		fseek(temp, 515 * 512 + 512 * block_num + 32, SEEK_SET);
		fwrite(&new_dir_2, 32, 1, temp);
		++group_desc.bg_used_dirs_count;
		update_group_desc();
		ext2_dir_entry new_dir;
		new_dir.file_type = 2;
		new_dir.inode = inode_num;
		strcpy(new_dir.name, sp[sp.size() - 1].c_str());
		new_dir.name_len = sp[sp.size() - 1].size();
		new_dir.rec_len = 32;
		if (tmp.i_blocks <= 6)
		{
			fseek(temp, 515 * 512 + 512 * tmp.i_block[tmp.i_blocks - 1] + tmp.i_size % 512, SEEK_SET);
			fwrite(&new_dir, 32, 1, temp);
		}
		else
		{
			if (tmp.i_blocks <= 6 + 256)
			{
				i_block_one one;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[6], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[tmp.i_blocks - 7] + tmp.i_size % 512, SEEK_SET);
				fwrite(&new_dir, 32, 1, temp);
			}
			else
			{
				i_block_one one, two;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[7], SEEK_SET);
				fread(&two, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * two.inode[(tmp.i_blocks - 7 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[(tmp.i_blocks - 7) % 256] + tmp.i_size % 512, SEEK_SET);
				fwrite(&new_dir, 32, 1, temp);
			}
		}
		tmp.i_size += 32;
		update_inode_entry(&tmp, tmp_num);
	}
	load_inode_entry(&current_inode, current_dir);
	return;
}

void rm(string filename, bool r, bool& c) //delete。r：递归地删除（用于删除文件夹）；c：为true时正常删除，为flase时不再进行递归删除（用于删目录时发现已打开文件的情况）
{
	load_inode_entry(&current_inode, current_dir);
	string d = "/";
	vector<string> sp = split(filename, d);
	for (auto& ele : sp)
	{
		if (ele == "." || ele == "..")
		{
			cout << "文件名有误。\n";
			return;
		}
	}
	if (filename == "/")
	{
		cout << "无法删除根目录。\n";
		return;
	}
	ext2_inode tmp;
	__u16 tmp_num;
	vector<ext2_dir_entry> dir_en;
	int i, j;
	if (filename[0] == '/')
	{
		load_inode_entry(&tmp, 1); //tmp = root
		tmp_num = 1;
	}
	else
	{
		tmp = current_inode;
		tmp_num = current_dir;
	}
	for (i = 0; i < sp.size(); ++i)
	{
		dir_en = load_all_dir(&tmp);
		for (j = 0; j < dir_en.size(); ++j)
		{
			if (!strcmp(dir_en[j].name, sp[i].c_str()))
			{
				break;
			}
		}
		if (i != sp.size() - 1 && (j == dir_en.size() || dir_en[j].file_type == 1))
		{
			cout << "路径无效\n";
			return;
		}
		if (i != sp.size() - 1)
		{
			load_inode_entry(&tmp, dir_en[j].inode);
			tmp_num = dir_en[j].inode;
		}
	}
	if (j == dir_en.size())
	{
		cout << "文件" << sp[sp.size() - 1] << "不存在。\n";
		return;
	}
	if (is_open(dir_en[j].inode))
	{
		cout << filename << "已打开，请先关闭后再删除。\n";
		c = false;
		return;
	}
	if (dir_en[j].file_type == 1) //删文件
	{
		ext2_inode del;
		load_inode_entry(&del, dir_en[j].inode);
		ext2_free_blocks(&del);
		ext2_free_inode(dir_en[j].inode);
	}
	else //删目录
	{
		if (!r)
		{
			cout << sp[sp.size() - 1] << "是目录，想删除请添加-r参数。\n";
			return;
		}
		ext2_inode del;
		load_inode_entry(&del, dir_en[j].inode);
		vector<ext2_dir_entry> del_dir = load_all_dir(&del);
		for (i = 2; i < del_dir.size() && c; ++i)
		{
			rm(filename + "/" + del_dir[i].name, true, c);
		}
		if (!c)
		{
			return;
		}
		ext2_free_blocks(&del);
		ext2_free_inode(dir_en[j].inode);
		--group_desc.bg_used_dirs_count;
	}
	time_t now;
	time(&now);
	if (j != dir_en.size() - 1) //删目录项
	{
		FILE* temp = FS;
		if (j < 6 * 16) //前6*16个目录项通过直接索引找到
		{
			fseek(temp, 515 * 512 + 512 * tmp.i_block[j / 16] + 32 * (j % 16), SEEK_SET);
			fwrite(&dir_en[dir_en.size() - 1], 32, 1, temp);
		}
		else
		{
			if (j < (6 + 256) * 16)
			{
				i_block_one one;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[6], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[j / 16 - 6] + 32 * (j % 16), SEEK_SET);
				fwrite(&dir_en[dir_en.size() - 1], 32, 1, temp);
			}
			else
			{
				i_block_one one, two;
				fseek(temp, 515 * 512 + 512 * tmp.i_block[7], SEEK_SET);
				fread(&two, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * two.inode[(j / 16 - 6 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, temp);
				fseek(temp, 515 * 512 + 512 * one.inode[(j / 16 - 6) % 256] + 32 * (j % 16), SEEK_SET);
				fwrite(&dir_en[dir_en.size() - 1], 32, 1, temp);
			}
		}
	}
	tmp.i_size -= 32;
	if (tmp.i_size % 512 == 0)
	{
		delete_a_block(&tmp, tmp_num);
	}
	tmp.i_atime = tmp.i_mtime = now;
	update_inode_entry(&tmp, tmp_num);
	if (!load_inode_entry(&current_inode, current_dir))
	{
		string root = "/";
		cd(root);
	}
	update_group_desc();
}

void cp(string f1, string f2, bool r, bool& c) //复制。r：递归地复制（用于复制文件夹）；c：为true时正常复制，为flase时不再进行递归复制（用于复制目录时发现剩余空间不足的情况）
{
	__u8 type1, type2;
	__u16 inode_num1 = search_filename(f1, type1);
	if (inode_num1 == 65535)
	{
		cout << f1 << "不存在。\n";
		return;
	}
	__u16 inode_num2 = search_filename(f2, type2);
	if (inode_num2 != 65535)
	{
		cout << f2 << "已存在。\n";
		return;
	}
	if (type1 == 1) //复制文件
	{
		ext2_inode inode1;
		load_inode_entry(&inode1, inode_num1);
		if (group_desc.bg_free_inodes_count == 0 || group_desc.bg_free_blocks_count < inode1.i_blocks + ceil((inode1.i_blocks - 6) / 256) + (inode1.i_blocks > 256 + 6))
		{
			cout << "剩余空间不足，无法完成复制操作。\n";
			c = false;
			return;
		}
		create(f2, 1);
		__u16 inode_num2 = search_filename(f2, type2);
		if (inode_num2 == 65535)
		{
			return;
		}
		ext2_inode inode2;
		load_inode_entry(&inode2, inode_num2);
		char ch;
		FILE* tmp = FS;
		unsigned int i;
		for (i = 0; i < inode1.i_size; ++i) //先读再写
		{
			if (i < 6 * 512) //直接索引
			{
				fseek(tmp, 515 * 512 + 512 * inode1.i_block[i / 512] + i % 512, SEEK_SET);
				fread(&ch, 1, 1, tmp);
			}
			else
			{
				if (i < 512 * (6 + 256)) //一次间接索引
				{
					i_block_one one;
					fseek(tmp, 515 * 512 + 512 * inode1.i_block[6], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[i / 512 - 6] + i % 512, SEEK_SET);
					fread(&ch, 1, 1, tmp);
				}
				else //二次间接索引
				{
					i_block_one one, two;
					fseek(tmp, 515 * 512 + 512 * inode1.i_block[7], SEEK_SET);
					fread(&two, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * two.inode[(i / 512 - 6 - 256) / 256], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[(i / 512 - 6) % 256] + i % 512, SEEK_SET);
					fread(&ch, 1, 1, tmp);
				}
			}
			if (inode2.i_size % 512 == 0)
			{
				add_a_block(&inode2, inode_num2);
			}
			if (inode2.i_size < 6 * 512) //直接索引
			{
				fseek(tmp, 515 * 512 + 512 * inode2.i_block[inode2.i_size / 512] + inode2.i_size % 512, SEEK_SET);
				fwrite(&ch, 1, 1, tmp);
			}
			else
			{
				if (inode2.i_size < 512 * (6 + 256)) //一次间接索引
				{
					i_block_one one;
					fseek(tmp, 515 * 512 + 512 * inode2.i_block[6], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[inode2.i_size / 512 - 6] + inode2.i_size % 512, SEEK_SET);
					fwrite(&ch, 1, 1, tmp);
				}
				else //二次间接索引
				{
					i_block_one one, two;
					fseek(tmp, 515 * 512 + 512 * inode2.i_block[7], SEEK_SET);
					fread(&two, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * two.inode[(inode2.i_size / 512 - 6 - 256) / 256], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[(inode2.i_size / 512 - 6) % 256] + inode2.i_size % 512, SEEK_SET);
					fwrite(&ch, 1, 1, tmp);
				}
			}
			++inode2.i_size;
			update_inode_entry(&inode2, inode_num2);
		}
	}
	else //复制目录
	{
		if (!r)
		{
			cout << "未指定-r，略过目录" << f1 << endl;
			return;
		}
		ext2_inode inode1;
		load_inode_entry(&inode1, inode_num1);
		if (group_desc.bg_free_inodes_count == 0 || group_desc.bg_free_blocks_count < inode1.i_blocks + ceil((inode1.i_blocks - 6) / 256) + (inode1.i_blocks > 256 + 6))
		{
			cout << "剩余空间不足，无法完成复制操作。\n";
			return;
		}
		create(f2, 2);
		vector<ext2_dir_entry> dir_en = load_all_dir(&inode1);
		for (int i = 2; i < dir_en.size() && c; ++i)
		{
			cp(f1 + "/" + dir_en[i].name, f2 + "/" + dir_en[i].name, true, c);
		}
	}
}

void mkdir(string& filename)
{
	create(filename, 2);
}

void rmdir(string& filename)
{
	__u8 type;
	__u16 inode_num = search_filename(filename, type);
	if (inode_num == 65535)
	{
		cout << filename << "不存在。\n";
		return;
	}
	if (type == 1)
	{
		cout << filename << "不是目录。\n";
		return;
	}
	ext2_inode file_inode;
	load_inode_entry(&file_inode, inode_num);
	if (file_inode.i_size > 64)
	{
		cout << filename << "目录非空。\n";
		return;
	}
	bool c = true;
	rm(filename, true, c);
}

int open(string& filename)
{
	int i, j, inode_num;
	__u8 type;
	inode_num = search_filename(filename, type);
	if (inode_num == 65535)
	{
		return -4; //文件不存在
	}
	if (type == 2)
	{
		cd(filename);
		return -2; //打开了一个目录
	}
	if (is_open(inode_num))
	{
		return -3; //文件已经打开
	}
	for (i = 0; i < 16; ++i)
	{
		if (fopen_table[i] == 65535)
		{
			break;
		}
	}
	if (i == 16)
	{
		return -1; //文件打开表已满
	}
	fopen_table[i] = inode_num;
	load_inode_entry(&open_inode[i], fopen_table[i]);
	return i;
}

bool close(int fd)
{
	if (fopen_table[fd] == 65535)
	{
		return false;
	}
	fopen_table[fd] = 65535;
	return true;
}

void read(int fd)
{
	if (fd >= 16 || fopen_table[fd] == 65535)
	{
		cout << "文件打开ID无效。\n";
		return;
	}
	FILE* tmp = FS;
	unsigned int i;
	char ch;
	for (i = 0; i < open_inode[fd].i_size; ++i)
	{
		if (i < 6 * 512) //直接索引
		{
			fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[i / 512] + i % 512, SEEK_SET);
			fread(&ch, 1, 1, tmp);
		}
		else
		{
			if (i < 512 * (6 + 256)) //一次间接索引
			{
				i_block_one one;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[6], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[i / 512 - 6] + i % 512, SEEK_SET);
				fread(&ch, 1, 1, tmp);
			}
			else //二次间接索引
			{
				i_block_one one, two;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[7], SEEK_SET);
				fread(&two, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * two.inode[(i / 512 - 6 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[(i / 512 - 6) % 256] + i % 512, SEEK_SET);
				fread(&ch, 1, 1, tmp);
			}
		}
		cout << ch;
	}
	cout << endl;
}

void write(int fd, bool conti) //conti: 不删除原有的内容，在文件尾继续写
{
	if (fd >= 16 || fopen_table[fd] == 65535)
	{
		cout << "文件打开ID无效。\n";
		return;
	}
	if (!conti)
	{
		ext2_free_blocks(&open_inode[fd]);
	}
	if (open_inode[fd].i_size % 512 == 0 && group_desc.bg_free_blocks_count == 0)
	{
		cout << "文件系统已满，无法继续完成写文件操作。\n";
		return;
	}
	FILE* tmp = FS;
	char ch = 0;
	do {
		if (ch == '\r')
		{
			ch = '\n';
		}
		else
		{
			ch = _getch();
		}
		if (ch == 27)
		{
			break;
		}
		cout << ch;
		if (open_inode[fd].i_size % 512 == 0)
		{
			add_a_block(&open_inode[fd], fopen_table[fd]);
		}
		if (open_inode[fd].i_size < 6 * 512) //直接索引
		{
			fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[open_inode[fd].i_size / 512] + open_inode[fd].i_size % 512, SEEK_SET);
			fwrite(&ch, 1, 1, tmp);
		}
		else
		{
			if (open_inode[fd].i_size < 512 * (6 + 256)) //一次间接索引
			{
				i_block_one one;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[6], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[open_inode[fd].i_size / 512 - 6] + open_inode[fd].i_size % 512, SEEK_SET);
				fwrite(&ch, 1, 1, tmp);
			}
			else //二次间接索引
			{
				i_block_one one, two;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[7], SEEK_SET);
				fread(&two, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * two.inode[(open_inode[fd].i_size / 512 - 6 - 256) / 256], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[(open_inode[fd].i_size / 512 - 6) % 256] + open_inode[fd].i_size % 512, SEEK_SET);
				fwrite(&ch, 1, 1, tmp);
			}
		}
		++open_inode[fd].i_size;
		update_inode_entry(&open_inode[fd], fopen_table[fd]);
		if (open_inode[fd].i_size % 512 == 0 && group_desc.bg_free_blocks_count == 0)
		{
			cout << "文件系统已满，无法继续完成写文件操作。\n";
			return;
		}
	} while (ch != 27); //输入ESC停止
	cout << endl;
}

void change_pw() //修改密码
{
	string old_pw, new_pw1, new_pw2;
	cout << "请输入原密码：";
	old_pw = get_pw();
	if (strcmp(old_pw.c_str(), group_desc.password))
	{
		cout << "\n密码错误！\n";
		return;
	}
	cout << "\n请输入新密码：";
	new_pw1 = get_pw();
	while (!new_pw1.size() || new_pw1.size() > 16)
	{
		cout << "\n密码长度为1~16位，请重新输入新密码：";
		new_pw1 = get_pw();
	}
	cout << "\n请再次输入新密码：";
	new_pw2 = get_pw();
	while (new_pw1 != new_pw2)
	{
		cout << "\n两次输入的密码不一致！";
		cout << "\n请输入新密码：";
		new_pw1 = get_pw();
		while (new_pw1.size() > 16)
		{
			cout << "\n密码长度最长为16位，请重新输入新密码：";
			new_pw1 = get_pw();
		}
		cout << "\n请再次输入新密码：";
		new_pw2 = get_pw();
	}
	strcpy(group_desc.password, new_pw1.c_str());
	update_group_desc();
	cout << endl;
	return;
}

void get_ID(string& filename)
{
	__u8 type;
	int inode_num = search_filename(filename, type);
	if (inode_num == 65535)
	{
		cout << "未找到" << filename << endl;
		return;
	}
	if (type == 2)
	{
		cout << filename << "是一个目录，而非文件。\n";
		return;
	}
	int i;
	for (i = 0; i < 16; ++i)
	{
		if (fopen_table[i] == inode_num)
		{
			cout << filename << "对应的文件打开ID为：" << i << "。\n";
			return;
		}
	}
	cout << filename << "尚未被打开。\n";
}

void shell()
{
	string command, d = " ", help = "--help";
	vector<string> cmd;
	vector<string> cmd_list = { "quit", "exit", "password", "format", "ls", "dir", "cd", "create", "rm", "delete" , "cp", "mkdir", "rmdir", "open", "close", "read", "write", "getID"};
	int i;
	while (1)
	{
		cout << "ylx@ylx:" << current_path << "$ ";
		getline(cin, command);
		cmd = split(command, d);
		if (cmd.size() == 0)
		{
			continue;
		}
		if (vector_find(cmd_list, cmd[0]) == cmd_list.size())
		{
			cout << "未找到" << cmd[0] << "命令！\n";
			cout << "可用命令包括：\n";
			for (i=0;i<cmd_list.size();++i)
			{
				cout << setw(16) << left << cmd_list[i];
				if (i % 4 == 3)
				{
					cout << endl;
				}
			}
			cout << "\n可使用 “[命令] --help” 查询使用帮助。\n\n";
		}
		if (cmd[0] == "quit" || cmd[0] == "exit")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法："<<cmd[0]<<endl;
				cout << "退出文件系统。\n";
				continue;
			}
			return;
		}
		if (cmd[0] == "password")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：password\n";
				cout << "修改文件系统密码。\n";
				continue;
			}
			change_pw();
		}
		if (cmd[0] == "format")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：format\n";
				cout << "格式化文件系统。\n";
				continue;
			}
			cout << "请问您真的要格式化文件系统吗？\n[Y / N]: ";
			char ch;
			cin >> ch;
			while (ch != 'Y' && ch != 'y' && ch != 'N' && ch != 'n')
			{
				cout << "非法输入，请重试：";
				cin >> ch;
			}
			if (ch == 'Y' || ch == 'y')
			{
				if (FS)
				{
					fclose(FS);
				}
				FS = NULL;
				initialize_memory();
				initialize_disk();
				cout << "文件系统已格式化。\n";
			}
			cin.ignore(numeric_limits<streamsize>::max(), '\n');
		}
		if (cmd[0] == "ls" || cmd[0] == "dir")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：" << cmd[0] << " [选项]... [文件]...\n";
				cout << "列出给定文件（默认为当前目录）的信息。\n\n";
				cout << "选项一览：\n";
				cout << setw(8) << left << "-a" << setw(32) << left << "不隐藏任何以.开始的项目" << endl;
				cout << setw(8) << left << "-l" << setw(32) << left << "列出详细信息" << endl;
				cout << setw(8) << left << "-S" << setw(32) << left << "按文件大小排序" << endl;
				cout << setw(8) << left << "-r" << setw(32) << left << "按逆序排序" << endl << endl;
				cout << "在未指定-S的情况下将按照文件名排序。\n";
				continue;
			}
			vector<__u16> inode_num;
			vector<__u8> type;
			vector<string> name;
			bool l, a, s, r;
			l = a = s = r = false;
			for (i = 1; i < cmd.size(); ++i)
			{
				if (cmd[i][0] == '-' && cmd[i].size()>1)
				{
					if (cmd[i][1] == 'l')
					{
						l = true;
					}
					if (cmd[i][1] == 'a')
					{
						a = true;
					}
					if (cmd[i][1] == 'S')
					{
						s = true;
					}
					if (cmd[i][1] == 'r')
					{
						r = true;
					}
					if (cmd[i][1] != 'l' && cmd[i][1] != 'a' && cmd[i][1] != 'S' && cmd[i][1] != 'r')
					{
						cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
						break;
					}
					continue;
				}
				__u8 type_tmp;
				inode_num.push_back(search_filename(cmd[i], type_tmp));
				type.push_back(type_tmp);
				name.push_back(cmd[i]);
			}
			if (i != cmd.size())
			{
				continue;
			}
			if (inode_num.size() == 0)
			{
				dir(current_inode, current_dir, l, a, s, r);
			}
			ext2_inode inode;
			for (i = 0; i < inode_num.size(); ++i)
			{
				load_inode_entry(&inode, inode_num[i]);
				if (type[i] == 1)
				{
					attrib(inode, name[i], l);
				}
				else
				{
					if (inode_num.size() > 1)
					{
						cout << name[i] << ": \n";
					}
					dir(inode, inode_num[i], l, a, s, r);
				}
			}
			cout << "\n";
		}
		if (cmd[0] == "cd")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：cd [目录]\n";
				cout << "改变shell工作目录\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			cd(cmd[1]);
		}
		if (cmd[0] == "create")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：create [文件名]\n";
				cout << "在当前目录或指定目录下新建一个名为[文件名]的文件。\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			create(cmd[1], 1);
		}
		if (cmd[0] == "rm" || cmd[0] == "delete")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：" << cmd[0] << " [选项]... [文件]...\n";
				cout << "删除指定<文件>。\n\n";
				cout << "选项一览：\n";
				cout << setw(8) << left << "-r" << setw(32) << left << "递归删除目录及其内容\n";
				continue;
			}
			if (cmd.size() == 1 || cmd.size() > 3 || cmd.size() == 3 && (cmd[1] != "-r" && cmd[2] != "-r" || cmd[1] == cmd[2]) || cmd.size() == 2 && cmd[1] == "-r")
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			bool c = true;
			if (cmd.size() == 2)
			{
				rm(cmd[1], false, c);
			}
			else
			{
				rm(((cmd[1] == "-r") ? cmd[2] : cmd[1]), true, c);
			}
		}
		if (cmd[0] == "cp")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：" << cmd[0] << " [选项]... 源文件 目标文件\n";
				cout << "将指定<源文件>复制到<目标文件>。\n\n";
				cout << "选项一览：\n";
				cout << setw(8) << left << "-r" << setw(32) << left << "递归复制目录及其子目录内的所有内容\n";
				continue;
			}
			int i, num_r = 0, pos = -1;
			for (i = 0; i < cmd.size(); ++i)
			{
				if (cmd[i] == "-r")
				{
					++num_r;
					pos = i;
				}
			}
			if (cmd.size() < 3 || cmd.size() > 4 || cmd.size() == 4 && num_r != 1 || cmd.size() == 3 && num_r != 0)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			bool c = true;
			if (cmd.size() == 3)
			{
				cp(cmd[1], cmd[2], false, c);
			}
			else
			{
				if (pos == 1)
				{
					cp(cmd[2], cmd[3], true, c);
				}
				if (pos == 2)
				{
					cp(cmd[1], cmd[3], true, c);
				}
				if (pos == 3)
				{
					cp(cmd[1], cmd[2], true, c);
				}
			}
		}
		if (cmd[0] == "mkdir")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：mkdir [目录名]\n";
				cout << "在当前目录或指定目录下新建一个名为[目录名]的目录。\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			mkdir(cmd[1]);
		}
		if (cmd[0] == "rmdir")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：rmdir [目录名]\n";
				cout << "删除指定<目录>。\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			rmdir(cmd[1]);
		}
		if (cmd[0] == "open")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：open [目录名/文件名]\n";
				cout << "输入[目录名]的效果与 “cd [目录名]” 一致，输入[文件名]会将该文件添加到文件打开表中，为后续读、写做准备。\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			int res = open(cmd[1]);
			if (res == -1)
			{
				cout << "文件打开表已满" << endl;
			}
			if (res == -2)
			{
				cout << cmd[1] << "是一个目录，将执行cd " << cmd[1] << endl;
			}
			if (res == -3)
			{
				cout << "文件已经被打开" << endl;
				get_ID(cmd[1]);
			}
			if (res == -4)
			{
				cout << "未找到" << cmd[1] << endl;
			}
			if (res >= 0)
			{
				cout << "已将" << cmd[1] << "添加在打开文件表中，文件打开ID为：" << res << endl;
			}
		}
		if (cmd[0] == "close")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：close [文件打开ID]\n";
				cout << "将[文件打开ID]对应的文件从文件打开表中移出。\n";
				continue;
			}
			if (cmd.size() != 2 || !is_num(cmd[1]))
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			close(stoi(cmd[1]));
		}
		if (cmd[0] == "read")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：read [文件打开ID]\n";
				cout << "读取[文件打开ID]对应的文件的内容。\n";
				continue;
			}
			if (cmd.size() != 2 || !is_num(cmd[1]))
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			read(stoi(cmd[1]));
		}
		if (cmd[0] == "write")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：write [选项] [文件打开ID]\n";
				cout << "写入[文件打开ID]对应的文件，直到按下ESC键。\n\n";
				cout << "选项一览：\n";
				cout << setw(8) << left << "-c" << setw(32) << left << "在文件尾继续写\n";
				continue;
			}
			if (cmd.size() > 3 || cmd.size() == 3 && !(cmd[1] == "-c" && is_num(cmd[2]) || cmd[2] == "-c" && is_num(cmd[1])) || cmd.size() == 2 && !is_num(cmd[1]))
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			if (cmd.size() == 2)
			{
				write(stoi(cmd[1]), false);
			}
			else
			{
				write(stoi(((cmd[1] == "-c") ? cmd[2] : cmd[1])), true);
			}
		}
		if (cmd[0] == "getID")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "用法：getID [文件名]\n";
				cout << "查找[文件名]对应的文件打开ID。\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "命令格式错误，请使用\"" << cmd[0] << " --help\"查看帮助。\n";
				continue;
			}
			get_ID(cmd[1]);
		}
	}
}

int main()
{	
	cout << "欢迎使用Yomi的类Ext2文件系统。\n";
	FS = fopen("FS", "rb+");
	if (!FS)
	{
		cout << "文件系统尚未创建，请问您要创建文件系统吗？\n[Y / N]: ";
		char ch;
		cin >> ch;
		while (ch != 'Y' && ch != 'y' && ch != 'N' && ch != 'n')
		{
			cout << "非法输入，请重试：";
			cin >> ch;
		}
		cin.ignore(numeric_limits<streamsize>::max(), '\n');
		if (ch == 'Y' || ch == 'y')
		{
			initialize_memory();
			initialize_disk();
		}
		else
		{
			cout << "感谢您的使用，再见。";
			return 0;
		}
	}
	else
	{
		update_memory();
	}
	string pw;
	cout << "请输入密码（初始密码为admin）：";
	pw = get_pw();
	cout << endl;
	while (strcmp(group_desc.password, pw.c_str()))
	{
		cout << "密码错误! 您希望退出文件系统还是格式化文件系统？\n[E: 退出/F: 格式化]: ";
		char ch;
		cin >> ch;
		while (ch != 'E' && ch != 'e' && ch != 'F' && ch != 'f')
		{
			cout << "非法输入，请重试：";
			cin >> ch;
		}
		cin.ignore(numeric_limits<streamsize>::max(), '\n');
		if (ch == 'F' || ch == 'f')
		{
			if (FS)
			{
				fclose(FS);
			}
			FS = NULL;
			initialize_memory();
			initialize_disk();
			cout << "文件系统已格式化。\n";
			cout << "请输入密码（初始密码为admin）：";
			pw = get_pw();
			cout << endl;
		}
		else
		{
			cout << "感谢您的使用，再见。";
			return 0;
		}
	}
	shell();
	cout << "感谢您的使用，再见。";
	if (FS)
	{
		update_group_desc();
		fclose(FS);
	}
	return 0;
}
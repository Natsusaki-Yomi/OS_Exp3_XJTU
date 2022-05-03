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

struct ext2_group_desc //����������С64�ֽ�
{
	char bg_volume_name[16]; //����
	__u16 bg_block_bitmap; //�����λͼ�Ŀ��
	__u16 bg_inode_bitmap; //�����������λͼ�Ŀ��
	__u16 bg_inode_table; //�����������ʼ���
	__u16 bg_free_blocks_count; //������п�ĸ���
	__u16 bg_free_inodes_count; //��������������ĸ���
	__u16 bg_used_dirs_count; //����Ŀ¼�ĸ���
	char password[17]; //����
	char bg_pad[18]; //���1
};

struct ext2_inode //�����ڵ㣬��С64�ֽ�
{
	__u16 i_mode; //�ļ����ͼ�����Ȩ��
	__u16 i_blocks; //�ļ������ݿ����
	__u32 i_size; //��С(�ֽ�)
	time_t i_atime; //����ʱ��
	time_t i_ctime; //����ʱ��
	time_t i_mtime; //�޸�ʱ��
	time_t i_dtime; //ɾ��ʱ��
	__u16 i_block[8]; //ָ�����ݿ��ָ��
	char i_pad[8]; //���1
};

struct ext2_dir_entry //Ŀ¼���С32�ֽ�
{
	__u16 inode; //�����ڵ��
	__u16 rec_len; //Ŀ¼���
	__u8 name_len; //�ļ�������
	__u8 file_type; //�ļ�����(1:��ͨ�ļ���2:Ŀ¼��)
	char name[EXT2_NAME_LEN]; //�ļ������26�ַ�
};

struct bitmap //λͼ
{
	__u8 map_8[512];
};

struct i_block_one //�༶�����д�����ݿ�ָ��Ŀ�
{
	__u16 inode[256];
};

__u16 fopen_table[16]; /*�ļ��򿪱�������ͬʱ��16���ļ�*/
ext2_inode open_inode[16];
__u16 last_alloc_inode; /*�ϴη������������*/
__u16 last_alloc_block; /*�ϴη�������ݿ��*/
char current_path[256]; /*��ǰ·��(�ַ���)*/
ext2_group_desc group_desc; /*��������*/
__u16 current_dir;  /*��ǰĿ¼��������㣩*/
ext2_inode current_inode; /*��ǰ�����ڵ�*/
FILE* FS; 

vector<string> split(string& s, string& d) //�ַ����ָ��
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

string i_mode_str(__u16 i_mode) //��0000000200000111תΪdrwxrwxrwx
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
	cout << "\n�������ݿ飺";
	cout << group_desc.bg_free_blocks_count;
	cout << "\n���������飺";
	cout << group_desc.bg_free_inodes_count;
	cout << "\nĿ¼������";
	cout << group_desc.bg_used_dirs_count;
	cout << "\n���룺";
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

bool update_group_desc() //���ڴ��е������������µ�"Ӳ��"
{
	FILE* tmp = FS;
	fseek(tmp, 0, SEEK_SET);
	fwrite(&group_desc, sizeof(ext2_group_desc), 1, tmp);
	return true;
}

bool reload_group_desc() //��������Ѹ��µ���������
{
	FILE* tmp = FS;
	fseek(tmp, 0, SEEK_SET);
	fread(&group_desc, sizeof(ext2_group_desc), 1, tmp);
	return true;
}

bool load_inode_entry(ext2_inode* obj, __u16 inode_num)  //�����ض���������㣬�������򷵻�false
{
	--inode_num; //��Ϊ��0��ʼ���
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if ((bm.map_8[inode_num / 8] >> (7 - inode_num % 8)) & 1) //�����ڵ㲻����
	{
		return false;
	}
	fseek(tmp, 3 * 512 + 64 * inode_num, SEEK_SET);
	fread(obj, 64, 1, tmp);
	return true;
}

bool update_inode_entry(ext2_inode* obj, __u16 inode_num) //�����ض����������
{
	--inode_num; //��Ϊ��0��ʼ���
	if (inode_num > 4095)
	{
		return false;
	}
	FILE* tmp = FS;
	bitmap bm;
	fseek(tmp, 2 * 512, SEEK_SET);
	fread(&bm, 512, 1, tmp);
	if ((bm.map_8[inode_num / 8] >> (7 - inode_num % 8)) & 1) //�����ڵ㲻����
	{
		return false;
	}
	else //�����ڵ��Ѵ��ڣ�����
	{		
		fseek(tmp, 3 * 512 + 64 * inode_num, SEEK_SET);
		fwrite(obj, 64, 1, tmp);
	}
	return true;
}

__u16 ext2_new_inode() //����һ���µ�������㣬�ɹ����������ڵ�ţ�ʧ�ܷ���65535
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
				if (((bm.map_8[i] << j) & 0b10000000) != 0) //bitmap��jλΪ1
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

__u16 ext2_alloc_block() //����һ���µ����ݿ飬�ɹ��������ݿ�ţ�ʧ�ܷ���65535
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
				if (((bm.map_8[i] << j) & 0b10000000) != 0) //bitmap��jλΪ1
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

bool ext2_free_inode(__u16 inode_num) //�ͷ��ض����������
{
	if (inode_num == 65535)
	{
		return false;
	}
	--inode_num; //��Ϊ��0��ʼ���
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

bool ext2_free_block_bitmap(__u16 block_num) //�ͷ��ض���ŵ����ݿ�λͼ
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

bool ext2_free_blocks(ext2_inode* obj) //�ͷ��ض��ļ����������ݿ�
{
	int i, j;
	if (obj->i_blocks <= 6) //ֱ������
	{
		for (i = 0; i < obj->i_blocks; ++i) //ɾֱ������
		{
			ext2_free_block_bitmap(obj->i_block[i]);
		}
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks <= 6 + 256) //һ������
		{
			for (i = 0; i < 6; ++i) //ɾֱ������
			{
				ext2_free_block_bitmap(obj->i_block[i]);
			}
			i_block_one one;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (i = 0; i < obj->i_blocks - 6; ++i) //ɾһ������
			{
				ext2_free_block_bitmap(one.inode[i]);
			}
			ext2_free_block_bitmap(obj->i_block[6]);
		}
		else //��������
		{
			for (i = 0; i < 6; ++i) //ɾֱ������
			{
				ext2_free_block_bitmap(obj->i_block[i]);
			}
			i_block_one one, two;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			for (i = 0; i < 256; ++i) //ɾһ������
			{
				ext2_free_block_bitmap(one.inode[i]);
			}
			ext2_free_block_bitmap(obj->i_block[6]);
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			for (i = j = 0; 256 + 6 + i * 256 < obj->i_blocks; ++i) //ɾ��������
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

bool add_a_block(ext2_inode* obj, __u16 inode_num) //Ϊ�ļ�����һ�����ݿ�
{
	if (group_desc.bg_free_blocks_count == 0)
	{
		return false;
	}
	if (group_desc.bg_free_blocks_count == 1 && obj->i_blocks % 256 == 6) //����Ҫ����һ��һ��������
	{
		return false;
	}
	if (group_desc.bg_free_blocks_count == 2 && obj->i_blocks == 256 + 6) //����Ҫ����һ������������
	{
		return false;
	}
	__u16 block_num = ext2_alloc_block();
	if (obj->i_blocks < 6) //ֱ������
	{
		obj->i_block[obj->i_blocks] = block_num;
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks < 6 + 256) //һ������
		{
			i_block_one one;
			if (obj->i_blocks == 6) //�½�һ������
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
		else //��������
		{
			i_block_one one, two;
			if (obj->i_blocks == 256 + 6) //�½���������
			{
				__u16 block_num_two = ext2_alloc_block();
				obj->i_block[7] = block_num_two;
			}
			else
			{
				fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
				fread(&two, 512, 1, tmp);
			}
			if (obj->i_blocks % 256 == 6) //�½�һ������
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

bool delete_a_block(ext2_inode* obj, __u16 inode_num) //ɾ�����һ�����ݿ�
{
	if (obj->i_blocks <= 6) //ֱ������
	{
		ext2_free_block_bitmap(obj->i_block[obj->i_blocks - 1]);
	}
	else
	{
		FILE* tmp = FS;
		if (obj->i_blocks <= 6 + 256) //һ������
		{
			i_block_one one;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[6], SEEK_SET);
			fread(&one, 512, 1, tmp);
			ext2_free_block_bitmap(one.inode[obj->i_blocks - 7]);
			if (obj->i_blocks == 7) //һ��ɾ��һ��������
			{
				ext2_free_block_bitmap(obj->i_block[6]);
			}
		}
		else //��������
		{
			i_block_one one, two;
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			fseek(tmp, 515 * 512 + 512 * (two.inode[(obj->i_blocks - 256 - 7) / 256]), SEEK_SET);
			fread(&one, 512, 1, tmp);
			ext2_free_block_bitmap(one.inode[(obj->i_blocks - 7) % 256]);
			if (obj->i_blocks % 256 == 7) //һ��ɾ��һ��������
			{
				ext2_free_block_bitmap(two.inode[(obj->i_blocks - 256 - 7) / 256]);
			}
			if (obj->i_blocks == 256 + 7) //һ��ɾ������������
			{
				ext2_free_block_bitmap(obj->i_block[7]);
			}
		}
	}
	--obj->i_blocks;
	update_inode_entry(obj, inode_num);
	return true;
}

vector<ext2_dir_entry> load_all_dir(ext2_inode* obj) //��ȡĿ¼������Ŀ¼��
{
	FILE* tmp = FS;
	vector<ext2_dir_entry> dir_en(obj->i_size / 32); //����obj->i_size/32��Ŀ¼��
	int i, j, k, cnt = 0;
	if (obj->i_blocks <= 6) //ǰ�������ͨ��ֱ�������ҵ�
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
		if (obj->i_blocks <= 6 + 128) //һ������
		{
			for (i = 0; i < 6; ++i) //ǰ�������ͨ��ֱ�������ҵ�
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
			for (; i < obj->i_blocks; ++i) //֮��Ŀ���һ������
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * one.inode[i - 6] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
		}
		else //��������
		{
			for (i = 0; i < 6; ++i) //ǰ�������ͨ��ֱ�������ҵ�
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
			for (; i < 256 + 6; ++i) //֮���256����һ������
			{
				for (j = 0; j < 16 && cnt < dir_en.size(); ++j, ++cnt)
				{
					fseek(tmp, 515 * 512 + 512 * one.inode[i - 6] + 32 * j, SEEK_SET);
					fread(&dir_en[cnt], 32, 1, tmp);
				}
			}
			fseek(tmp, 515 * 512 + 512 * obj->i_block[7], SEEK_SET);
			fread(&two, 512, 1, tmp);
			for (j = 0; 256 + 6 + 256 * j < obj->i_blocks; ++j) //֮��Ŀ��ö�������
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

__u16 search_filename(string& filename, __u8& type) //�����ļ��������ļ���Ӧ�������ڵ�ţ��ļ��������򷵻�65535
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
	for (i = 0; i < 16; ++i) //�ļ��򿪱����
	{
		fopen_table[i] = 65535;
	}
	last_alloc_inode = 1; //�ϴη�����1�������ڵ㣨��Ŀ¼��
	last_alloc_block = 0; //�ϴη�����0�����ݿ飨��Ŀ¼��
	current_dir = 1; //��ǰ����1�������ڵ㣨��Ŀ¼��
	strcpy(current_path, (char*)"/");
	strcpy(group_desc.bg_volume_name, (char*)"ylx");
	strcpy(group_desc.password, (char*)"admin");
	group_desc.bg_block_bitmap = 1;
	group_desc.bg_inode_bitmap = 2;
	group_desc.bg_inode_table = 3;
	group_desc.bg_free_inodes_count = 4095;
	group_desc.bg_free_blocks_count = 4095;
	group_desc.bg_used_dirs_count = 1;
	for (i = 0; i < 18; ++i) //���0xFF
	{
		group_desc.bg_pad[i] = 0b1111111;
	}
}

void initialize_disk() //�����ļ���������Ŀ¼
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
	bm.map_8[0] = 0b01111111; //��λͼ��һλ������Ϊ0
	fseek(tmp, 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp); //д���ݿ�λͼ
	fseek(tmp, 2 * 512, SEEK_SET);
	fwrite(&bm, 512, 1, tmp); //д�����ڵ�λͼ
	time_t now; //��ǰʱ��
	time(&now);
	ext2_dir_entry root_dir_1, root_dir_2; //��Ŀ¼�µĵ�ǰĿ¼�͸�Ŀ¼
	root_dir_1.file_type = root_dir_2.file_type = 2;
	strcpy(root_dir_1.name, ".");
	strcpy(root_dir_2.name, "..");
	root_dir_1.name_len = 1;
	root_dir_2.name_len = 2;
	root_dir_1.rec_len = root_dir_2.rec_len = sizeof(ext2_dir_entry); //��С�̶�Ϊ32�ֽ�
	root_dir_1.inode = root_dir_2.inode = 1; //��Ŀ¼�������ڵ��Ϊ1
	fseek(tmp, 515 * 512, SEEK_SET);
	fwrite(&root_dir_1, sizeof(root_dir_1), 1, tmp);
	fseek(tmp, 515 * 512 + sizeof(root_dir_1), SEEK_SET);
	fwrite(&root_dir_2, sizeof(root_dir_2), 1, tmp);
	ext2_inode root; //������Ŀ¼
	root.i_atime = root.i_ctime = root.i_mtime = now;
	root.i_dtime = 0; //����ʱ����Ϣ
	root.i_mode = 0x0206; //drw-rw-rw-
	root.i_size = 2 * sizeof(ext2_dir_entry); //����Ŀ¼����ݴ�СΪ64�ֽ�
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
	for (i = 0; i < 16; ++i) //�ļ��򿪱����
	{
		fopen_table[i] = 65535;
	}
	last_alloc_inode = 1; //�ϴη�����1�������ڵ㣨��Ŀ¼��
	last_alloc_block = 0; //�ϴη�����0�����ݿ飨��Ŀ¼��
	//����������ʵ������
	current_dir = 1;
	strcpy(current_path, (char*)"/");
	reload_group_desc();
	load_inode_entry(&current_inode, 1);
}

void dir(ext2_inode& dir_inode, __u16 dir_num, bool l, bool a, bool s, bool r) //ls��dir��Ч����Ϊ�г�dir_inode�������ļ���l����ϸ��Ϣ��a�������ļ���s������С����r������
{
	FILE* tmp = FS;
	int i;
	vector<vector<string>> res; //resÿһ��Ϊ{Ȩ�ޣ��ļ�������С������ʱ�䣬����ʱ�䣬�޸�ʱ��}
	time_t now;
	time(&now);
	dir_inode.i_atime = now;
	update_inode_entry(&dir_inode, dir_num);
	vector<ext2_dir_entry> dir_en = load_all_dir(&dir_inode); //����Ŀ¼��
	if (!l)
	{
		for (auto& item : dir_en)
		{
			if (item.name[0] == '.') //�����ļ�
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
			if (item.name[0] == '.' && a || item.name[0] != '.') //�����ļ�
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
		cout << setw(12) << left << "����/Ȩ��" << setw(28) << left << "�ļ���" << setw(16) << left << "�ļ���С" << setw(32) << left << "����ʱ��" << setw(32) << left << "����ʱ��" << setw(32) << left << "�޸�ʱ��" << endl;
		for (i = 0; i < res.size(); ++i)
		{
			cout << setw(12) << left << res[i][0] << setw(28) << left << res[i][1] << setw(16) << left << res[i][2] << setw(32) << left << res[i][3] << setw(32) << left << res[i][4] << setw(32) << left << res[i][5] << endl;
		}
	}
	load_inode_entry(&current_inode, current_dir);
}

void attrib(ext2_inode& attrib_inode, string& filename, bool l) //l: ��ϸ��Ϣ
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
	cout << setw(12) << left << "����/Ȩ��" << setw(28) << left << "�ļ���" << setw(16) << left << "�ļ���С" << setw(32) << left << "����ʱ��" << setw(32) << left << "����ʱ��" << setw(32) << left << "�޸�ʱ��" << endl;
	cout << setw(12) << left << i_mode_str(attrib_inode.i_mode) << setw(28) << left << filename << setw(16) << left << to_string(attrib_inode.i_size) << setw(32) << left << ct << setw(32) << left << at << setw(32) << left << mt << endl;
	return;
}

void cd(string& filename) //�л���ǰĿ¼
{
	load_inode_entry(&current_inode, current_dir);
	__u8 type;
	__u16 inode = search_filename(filename, type);
	time_t now;
	time(&now);
	if (inode == 65535)
	{
		cout << "��������Ϊ" << filename << "���ļ���Ŀ¼��\n";
		return;
	}
	if (type == 1)
	{
		cout << filename << "����һ��Ŀ¼��\n";
		return;
	}
	if (inode == current_dir) //���ص�ǰĿ¼
	{
		current_inode.i_atime = now;
		update_inode_entry(&current_inode, inode);
		return;
	}
	if (filename[0] == '/')
	{
		if (filename.size() > 255)
		{
			cout << "·����������\n";
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
				cout << "·����������\n";
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
					cout << "·����������\n";
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

void create(string& filename, __u8 type) //create(�ļ������ļ�����)
{
	load_inode_entry(&current_inode, current_dir);
	string d = "/";
	vector<string> sp = split(filename, d);
	if (!sp.size())
	{
		cout << "�ļ��������Ϲ淶��\n";
		return;
	}
	if (sp[sp.size() - 1].size() > 25)
	{
		cout << "�ļ����������Ϊ25��\n";
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
			cout << "·����Ч\n";
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
		cout << "�ļ��Ѵ��ڡ�\n";
		return;
	}
	if (group_desc.bg_free_inodes_count == 0)
	{
		cout << "�ռ��������޷������ļ���\n";
		return;
	}
	__u16 inode_num = ext2_new_inode();
	ext2_inode ct;
	time_t now;
	time(&now);
	if (type == 1) //�ļ�
	{
		if (tmp.i_size % 512 == 0)
		{
			if (!add_a_block(&tmp, tmp_num))
			{
				cout << "�ռ��������޷������ļ���\n";
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
	else //Ŀ¼
	{
		if (group_desc.bg_free_blocks_count == 0)
		{
			cout << "�ռ��������޷�����Ŀ¼��\n";
			ext2_free_inode(inode_num);
			return;
		}
		if (tmp.i_size % 512 == 0)
		{
			if (!add_a_block(&tmp, tmp_num))
			{
				cout << "�ռ��������޷�����Ŀ¼��\n";
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
		ext2_dir_entry new_dir_1, new_dir_2; //��Ŀ¼�µĵ�ǰĿ¼�͸�Ŀ¼
		new_dir_1.file_type = new_dir_2.file_type = 2;
		strcpy(new_dir_1.name, ".");
		strcpy(new_dir_2.name, "..");
		new_dir_1.name_len = 1;
		new_dir_2.name_len = 2;
		new_dir_1.rec_len = new_dir_2.rec_len = 32; //��С�̶�Ϊ32�ֽ�
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

void rm(string filename, bool r, bool& c) //delete��r���ݹ��ɾ��������ɾ���ļ��У���c��Ϊtrueʱ����ɾ����Ϊflaseʱ���ٽ��еݹ�ɾ��������ɾĿ¼ʱ�����Ѵ��ļ��������
{
	load_inode_entry(&current_inode, current_dir);
	string d = "/";
	vector<string> sp = split(filename, d);
	for (auto& ele : sp)
	{
		if (ele == "." || ele == "..")
		{
			cout << "�ļ�������\n";
			return;
		}
	}
	if (filename == "/")
	{
		cout << "�޷�ɾ����Ŀ¼��\n";
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
			cout << "·����Ч\n";
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
		cout << "�ļ�" << sp[sp.size() - 1] << "�����ڡ�\n";
		return;
	}
	if (is_open(dir_en[j].inode))
	{
		cout << filename << "�Ѵ򿪣����ȹرպ���ɾ����\n";
		c = false;
		return;
	}
	if (dir_en[j].file_type == 1) //ɾ�ļ�
	{
		ext2_inode del;
		load_inode_entry(&del, dir_en[j].inode);
		ext2_free_blocks(&del);
		ext2_free_inode(dir_en[j].inode);
	}
	else //ɾĿ¼
	{
		if (!r)
		{
			cout << sp[sp.size() - 1] << "��Ŀ¼����ɾ�������-r������\n";
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
	if (j != dir_en.size() - 1) //ɾĿ¼��
	{
		FILE* temp = FS;
		if (j < 6 * 16) //ǰ6*16��Ŀ¼��ͨ��ֱ�������ҵ�
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

void cp(string f1, string f2, bool r, bool& c) //���ơ�r���ݹ�ظ��ƣ����ڸ����ļ��У���c��Ϊtrueʱ�������ƣ�Ϊflaseʱ���ٽ��еݹ鸴�ƣ����ڸ���Ŀ¼ʱ����ʣ��ռ䲻��������
{
	__u8 type1, type2;
	__u16 inode_num1 = search_filename(f1, type1);
	if (inode_num1 == 65535)
	{
		cout << f1 << "�����ڡ�\n";
		return;
	}
	__u16 inode_num2 = search_filename(f2, type2);
	if (inode_num2 != 65535)
	{
		cout << f2 << "�Ѵ��ڡ�\n";
		return;
	}
	if (type1 == 1) //�����ļ�
	{
		ext2_inode inode1;
		load_inode_entry(&inode1, inode_num1);
		if (group_desc.bg_free_inodes_count == 0 || group_desc.bg_free_blocks_count < inode1.i_blocks + ceil((inode1.i_blocks - 6) / 256) + (inode1.i_blocks > 256 + 6))
		{
			cout << "ʣ��ռ䲻�㣬�޷���ɸ��Ʋ�����\n";
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
		for (i = 0; i < inode1.i_size; ++i) //�ȶ���д
		{
			if (i < 6 * 512) //ֱ������
			{
				fseek(tmp, 515 * 512 + 512 * inode1.i_block[i / 512] + i % 512, SEEK_SET);
				fread(&ch, 1, 1, tmp);
			}
			else
			{
				if (i < 512 * (6 + 256)) //һ�μ������
				{
					i_block_one one;
					fseek(tmp, 515 * 512 + 512 * inode1.i_block[6], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[i / 512 - 6] + i % 512, SEEK_SET);
					fread(&ch, 1, 1, tmp);
				}
				else //���μ������
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
			if (inode2.i_size < 6 * 512) //ֱ������
			{
				fseek(tmp, 515 * 512 + 512 * inode2.i_block[inode2.i_size / 512] + inode2.i_size % 512, SEEK_SET);
				fwrite(&ch, 1, 1, tmp);
			}
			else
			{
				if (inode2.i_size < 512 * (6 + 256)) //һ�μ������
				{
					i_block_one one;
					fseek(tmp, 515 * 512 + 512 * inode2.i_block[6], SEEK_SET);
					fread(&one, 512, 1, tmp);
					fseek(tmp, 515 * 512 + 512 * one.inode[inode2.i_size / 512 - 6] + inode2.i_size % 512, SEEK_SET);
					fwrite(&ch, 1, 1, tmp);
				}
				else //���μ������
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
	else //����Ŀ¼
	{
		if (!r)
		{
			cout << "δָ��-r���Թ�Ŀ¼" << f1 << endl;
			return;
		}
		ext2_inode inode1;
		load_inode_entry(&inode1, inode_num1);
		if (group_desc.bg_free_inodes_count == 0 || group_desc.bg_free_blocks_count < inode1.i_blocks + ceil((inode1.i_blocks - 6) / 256) + (inode1.i_blocks > 256 + 6))
		{
			cout << "ʣ��ռ䲻�㣬�޷���ɸ��Ʋ�����\n";
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
		cout << filename << "�����ڡ�\n";
		return;
	}
	if (type == 1)
	{
		cout << filename << "����Ŀ¼��\n";
		return;
	}
	ext2_inode file_inode;
	load_inode_entry(&file_inode, inode_num);
	if (file_inode.i_size > 64)
	{
		cout << filename << "Ŀ¼�ǿա�\n";
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
		return -4; //�ļ�������
	}
	if (type == 2)
	{
		cd(filename);
		return -2; //����һ��Ŀ¼
	}
	if (is_open(inode_num))
	{
		return -3; //�ļ��Ѿ���
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
		return -1; //�ļ��򿪱�����
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
		cout << "�ļ���ID��Ч��\n";
		return;
	}
	FILE* tmp = FS;
	unsigned int i;
	char ch;
	for (i = 0; i < open_inode[fd].i_size; ++i)
	{
		if (i < 6 * 512) //ֱ������
		{
			fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[i / 512] + i % 512, SEEK_SET);
			fread(&ch, 1, 1, tmp);
		}
		else
		{
			if (i < 512 * (6 + 256)) //һ�μ������
			{
				i_block_one one;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[6], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[i / 512 - 6] + i % 512, SEEK_SET);
				fread(&ch, 1, 1, tmp);
			}
			else //���μ������
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

void write(int fd, bool conti) //conti: ��ɾ��ԭ�е����ݣ����ļ�β����д
{
	if (fd >= 16 || fopen_table[fd] == 65535)
	{
		cout << "�ļ���ID��Ч��\n";
		return;
	}
	if (!conti)
	{
		ext2_free_blocks(&open_inode[fd]);
	}
	if (open_inode[fd].i_size % 512 == 0 && group_desc.bg_free_blocks_count == 0)
	{
		cout << "�ļ�ϵͳ�������޷��������д�ļ�������\n";
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
		if (open_inode[fd].i_size < 6 * 512) //ֱ������
		{
			fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[open_inode[fd].i_size / 512] + open_inode[fd].i_size % 512, SEEK_SET);
			fwrite(&ch, 1, 1, tmp);
		}
		else
		{
			if (open_inode[fd].i_size < 512 * (6 + 256)) //һ�μ������
			{
				i_block_one one;
				fseek(tmp, 515 * 512 + 512 * open_inode[fd].i_block[6], SEEK_SET);
				fread(&one, 512, 1, tmp);
				fseek(tmp, 515 * 512 + 512 * one.inode[open_inode[fd].i_size / 512 - 6] + open_inode[fd].i_size % 512, SEEK_SET);
				fwrite(&ch, 1, 1, tmp);
			}
			else //���μ������
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
			cout << "�ļ�ϵͳ�������޷��������д�ļ�������\n";
			return;
		}
	} while (ch != 27); //����ESCֹͣ
	cout << endl;
}

void change_pw() //�޸�����
{
	string old_pw, new_pw1, new_pw2;
	cout << "������ԭ���룺";
	old_pw = get_pw();
	if (strcmp(old_pw.c_str(), group_desc.password))
	{
		cout << "\n�������\n";
		return;
	}
	cout << "\n�����������룺";
	new_pw1 = get_pw();
	while (!new_pw1.size() || new_pw1.size() > 16)
	{
		cout << "\n���볤��Ϊ1~16λ�����������������룺";
		new_pw1 = get_pw();
	}
	cout << "\n���ٴ����������룺";
	new_pw2 = get_pw();
	while (new_pw1 != new_pw2)
	{
		cout << "\n������������벻һ�£�";
		cout << "\n�����������룺";
		new_pw1 = get_pw();
		while (new_pw1.size() > 16)
		{
			cout << "\n���볤���Ϊ16λ�����������������룺";
			new_pw1 = get_pw();
		}
		cout << "\n���ٴ����������룺";
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
		cout << "δ�ҵ�" << filename << endl;
		return;
	}
	if (type == 2)
	{
		cout << filename << "��һ��Ŀ¼�������ļ���\n";
		return;
	}
	int i;
	for (i = 0; i < 16; ++i)
	{
		if (fopen_table[i] == inode_num)
		{
			cout << filename << "��Ӧ���ļ���IDΪ��" << i << "��\n";
			return;
		}
	}
	cout << filename << "��δ���򿪡�\n";
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
			cout << "δ�ҵ�" << cmd[0] << "���\n";
			cout << "�������������\n";
			for (i=0;i<cmd_list.size();++i)
			{
				cout << setw(16) << left << cmd_list[i];
				if (i % 4 == 3)
				{
					cout << endl;
				}
			}
			cout << "\n��ʹ�� ��[����] --help�� ��ѯʹ�ð�����\n\n";
		}
		if (cmd[0] == "quit" || cmd[0] == "exit")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���"<<cmd[0]<<endl;
				cout << "�˳��ļ�ϵͳ��\n";
				continue;
			}
			return;
		}
		if (cmd[0] == "password")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���password\n";
				cout << "�޸��ļ�ϵͳ���롣\n";
				continue;
			}
			change_pw();
		}
		if (cmd[0] == "format")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���format\n";
				cout << "��ʽ���ļ�ϵͳ��\n";
				continue;
			}
			cout << "���������Ҫ��ʽ���ļ�ϵͳ��\n[Y / N]: ";
			char ch;
			cin >> ch;
			while (ch != 'Y' && ch != 'y' && ch != 'N' && ch != 'n')
			{
				cout << "�Ƿ����룬�����ԣ�";
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
				cout << "�ļ�ϵͳ�Ѹ�ʽ����\n";
			}
			cin.ignore(numeric_limits<streamsize>::max(), '\n');
		}
		if (cmd[0] == "ls" || cmd[0] == "dir")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���" << cmd[0] << " [ѡ��]... [�ļ�]...\n";
				cout << "�г������ļ���Ĭ��Ϊ��ǰĿ¼������Ϣ��\n\n";
				cout << "ѡ��һ����\n";
				cout << setw(8) << left << "-a" << setw(32) << left << "�������κ���.��ʼ����Ŀ" << endl;
				cout << setw(8) << left << "-l" << setw(32) << left << "�г���ϸ��Ϣ" << endl;
				cout << setw(8) << left << "-S" << setw(32) << left << "���ļ���С����" << endl;
				cout << setw(8) << left << "-r" << setw(32) << left << "����������" << endl << endl;
				cout << "��δָ��-S������½������ļ�������\n";
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
						cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
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
				cout << "�÷���cd [Ŀ¼]\n";
				cout << "�ı�shell����Ŀ¼\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			cd(cmd[1]);
		}
		if (cmd[0] == "create")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���create [�ļ���]\n";
				cout << "�ڵ�ǰĿ¼��ָ��Ŀ¼���½�һ����Ϊ[�ļ���]���ļ���\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			create(cmd[1], 1);
		}
		if (cmd[0] == "rm" || cmd[0] == "delete")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���" << cmd[0] << " [ѡ��]... [�ļ�]...\n";
				cout << "ɾ��ָ��<�ļ�>��\n\n";
				cout << "ѡ��һ����\n";
				cout << setw(8) << left << "-r" << setw(32) << left << "�ݹ�ɾ��Ŀ¼��������\n";
				continue;
			}
			if (cmd.size() == 1 || cmd.size() > 3 || cmd.size() == 3 && (cmd[1] != "-r" && cmd[2] != "-r" || cmd[1] == cmd[2]) || cmd.size() == 2 && cmd[1] == "-r")
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
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
				cout << "�÷���" << cmd[0] << " [ѡ��]... Դ�ļ� Ŀ���ļ�\n";
				cout << "��ָ��<Դ�ļ�>���Ƶ�<Ŀ���ļ�>��\n\n";
				cout << "ѡ��һ����\n";
				cout << setw(8) << left << "-r" << setw(32) << left << "�ݹ鸴��Ŀ¼������Ŀ¼�ڵ���������\n";
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
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
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
				cout << "�÷���mkdir [Ŀ¼��]\n";
				cout << "�ڵ�ǰĿ¼��ָ��Ŀ¼���½�һ����Ϊ[Ŀ¼��]��Ŀ¼��\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			mkdir(cmd[1]);
		}
		if (cmd[0] == "rmdir")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���rmdir [Ŀ¼��]\n";
				cout << "ɾ��ָ��<Ŀ¼>��\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			rmdir(cmd[1]);
		}
		if (cmd[0] == "open")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���open [Ŀ¼��/�ļ���]\n";
				cout << "����[Ŀ¼��]��Ч���� ��cd [Ŀ¼��]�� һ�£�����[�ļ���]�Ὣ���ļ���ӵ��ļ��򿪱��У�Ϊ��������д��׼����\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			int res = open(cmd[1]);
			if (res == -1)
			{
				cout << "�ļ��򿪱�����" << endl;
			}
			if (res == -2)
			{
				cout << cmd[1] << "��һ��Ŀ¼����ִ��cd " << cmd[1] << endl;
			}
			if (res == -3)
			{
				cout << "�ļ��Ѿ�����" << endl;
				get_ID(cmd[1]);
			}
			if (res == -4)
			{
				cout << "δ�ҵ�" << cmd[1] << endl;
			}
			if (res >= 0)
			{
				cout << "�ѽ�" << cmd[1] << "����ڴ��ļ����У��ļ���IDΪ��" << res << endl;
			}
		}
		if (cmd[0] == "close")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���close [�ļ���ID]\n";
				cout << "��[�ļ���ID]��Ӧ���ļ����ļ��򿪱����Ƴ���\n";
				continue;
			}
			if (cmd.size() != 2 || !is_num(cmd[1]))
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			close(stoi(cmd[1]));
		}
		if (cmd[0] == "read")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���read [�ļ���ID]\n";
				cout << "��ȡ[�ļ���ID]��Ӧ���ļ������ݡ�\n";
				continue;
			}
			if (cmd.size() != 2 || !is_num(cmd[1]))
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			read(stoi(cmd[1]));
		}
		if (cmd[0] == "write")
		{
			if (vector_find(cmd, help) != cmd.size())
			{
				cout << "�÷���write [ѡ��] [�ļ���ID]\n";
				cout << "д��[�ļ���ID]��Ӧ���ļ���ֱ������ESC����\n\n";
				cout << "ѡ��һ����\n";
				cout << setw(8) << left << "-c" << setw(32) << left << "���ļ�β����д\n";
				continue;
			}
			if (cmd.size() > 3 || cmd.size() == 3 && !(cmd[1] == "-c" && is_num(cmd[2]) || cmd[2] == "-c" && is_num(cmd[1])) || cmd.size() == 2 && !is_num(cmd[1]))
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
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
				cout << "�÷���getID [�ļ���]\n";
				cout << "����[�ļ���]��Ӧ���ļ���ID��\n";
				continue;
			}
			if (cmd.size() != 2)
			{
				cout << "�����ʽ������ʹ��\"" << cmd[0] << " --help\"�鿴������\n";
				continue;
			}
			get_ID(cmd[1]);
		}
	}
}

int main()
{	
	cout << "��ӭʹ��Yomi����Ext2�ļ�ϵͳ��\n";
	FS = fopen("FS", "rb+");
	if (!FS)
	{
		cout << "�ļ�ϵͳ��δ������������Ҫ�����ļ�ϵͳ��\n[Y / N]: ";
		char ch;
		cin >> ch;
		while (ch != 'Y' && ch != 'y' && ch != 'N' && ch != 'n')
		{
			cout << "�Ƿ����룬�����ԣ�";
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
			cout << "��л����ʹ�ã��ټ���";
			return 0;
		}
	}
	else
	{
		update_memory();
	}
	string pw;
	cout << "���������루��ʼ����Ϊadmin����";
	pw = get_pw();
	cout << endl;
	while (strcmp(group_desc.password, pw.c_str()))
	{
		cout << "�������! ��ϣ���˳��ļ�ϵͳ���Ǹ�ʽ���ļ�ϵͳ��\n[E: �˳�/F: ��ʽ��]: ";
		char ch;
		cin >> ch;
		while (ch != 'E' && ch != 'e' && ch != 'F' && ch != 'f')
		{
			cout << "�Ƿ����룬�����ԣ�";
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
			cout << "�ļ�ϵͳ�Ѹ�ʽ����\n";
			cout << "���������루��ʼ����Ϊadmin����";
			pw = get_pw();
			cout << endl;
		}
		else
		{
			cout << "��л����ʹ�ã��ټ���";
			return 0;
		}
	}
	shell();
	cout << "��л����ʹ�ã��ټ���";
	if (FS)
	{
		update_group_desc();
		fclose(FS);
	}
	return 0;
}
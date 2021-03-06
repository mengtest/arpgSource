#include "logind_cache.h"
#include "map_manager.h"
#include "logind_player.h"
#include "logind_mongodb.h"
#include <shared/util.h>

#define OBJECT_END_STRING "------------------"

#define GAME_SERVER_SAVE_PLAYER_TIMEOUT 300
#define PK_SERVER_SAVE_PLAYER_TIMEOUT 3

LogindCache::LogindCache()
{
	m_storage = new core_obj::Storage([](string guid){
		//根据guid来区分要new什么样的binlog
		BinLogObject *ptr = NULL;
		switch (GuidManager::GetPrefix(guid))
		{
		case ObjectTypePlayer:
			ptr = new logind_player;
			break;
		default:
			if(guid == GLOBAL_MAP_INSTANCE_LIST_GUID)
			{
				ASSERT(!LogindApp::g_app->m_map_Instance_list);
				LogindApp::g_app->m_map_Instance_list = new MapManager;
				ptr = LogindApp::g_app->m_map_Instance_list;
				break;
			}

			if(guid == GLOBAL_SAVE_TO_DB)
			{
				if(!LogindApp::g_app->m_save_to_db_guid_list)
					LogindApp::g_app->m_save_to_db_guid_list = new SavePlayerGuidObj;
				ptr = LogindApp::g_app->m_save_to_db_guid_list;
				break;
			}

			//相当于略过
			if(guid == GLOBAL_SERVER_CONNN_LIST_GUID)
			{
				ptr = new BinLogObject;
				break;
			}

			ptr = LogindApp::g_app->CreateGlobalValues(LogindApp::g_app->m_obj_mgr, guid);
			if(!ptr)
			{
				ptr = new BinLogObject;
			}
			break;
		}
		ptr->guid(guid);
		return ptr;
	});
	//m_storage->Goto(g_Config.backup_hdd_path);
}

LogindCache::~LogindCache()
{
	for(auto it = m_accountMaps.begin();it != m_accountMaps.end();++it)
	{
		free(it->second);
	}
	m_accountMaps.clear();
	safe_delete(m_storage);
}

void LogindCache::AddAccount(account_table *info)
{
	//ASSERT(!GetAccount(info->name));
	auto account = GetAccount(info->name);
	if(account)
		safe_delete(account);
	m_accountMaps[info->name] = info;
}

account_table *LogindCache::GetAccount(const string &account)
{
	auto it = m_accountMaps.find(account);
	if(it != m_accountMaps.end())		
		return it->second;
	return NULL;
}

void LogindCache::GetCharList(const string &account,vector<char_create_info>& result)
{
	if(!LogindApp::g_app->IsGameServer())
		return;
	result.clear();

	//从角色在线缓存取得角色列表
	auto it = m_charListHD.find(account);
	if(it != m_charListHD.end() && strlen(it->second.guid) > 0)
	{				
		result.push_back(it->second);
		return;
	}
}

void LogindCache::AddCharName(const string &guid, const string &name)
{
	m_charNameMaps[name] = guid;
	m_storage->Goto(g_Config.player_data_hdd_path);
	m_storage->AddContent("char_name.txt", guid + ' ' + name + '\n');
}

const string &LogindCache::FindGuidByCharName(const string &name)
{
	auto it = m_charNameMaps.find(name);
	if(it != m_charNameMaps.end())
		return it->second;
	static string null_res = "";
	return null_res;
}

//读取所有的角色名
void LogindCache::LoadAllCharName()
{
	m_storage->Goto(g_Config.player_data_hdd_path);
	m_storage->ReadFile("char_name.txt", [&](const string &str){
		Tokens tokens;
		StrSplit(tokens, str, ' ');
		if(tokens.size() < 2)
			return true;
		m_charNameMaps[tokens[1]] = tokens[0];
		return true;
	});
}

void LogindCache::AddAccountToChar(const string& account,char_create_info& info)
{
	m_charListHD[account] = info;
}

void LogindCache::AddAccountToChar(const string& account,string guid)
{
	char_create_info info;
	memset(&info, 0 ,sizeof(info));
	strcpy(info.guid,guid.c_str());	
	m_charListHD[account] = info;
}

//保存合服标志文件
void LogindCache::SaveFileToMergeServer(const string& file_name, const string& now_time)
{
	//m_storage->SaveFile(file_name,now_time);
}


//释放登出的玩家缓存
void LogindCache::FreeLogoutPlayer(bool immediately)
{
	time_t t = time(nullptr);
	for (auto it = m_logoutChar.begin(); it != m_logoutChar.end();)
	{
		if(immediately
			|| (LogindApp::g_app->IsGameServer() && t > it->second + GAME_SERVER_SAVE_PLAYER_TIMEOUT)
			|| (LogindApp::g_app->IsPKServer() && t > it->second + PK_SERVER_SAVE_PLAYER_TIMEOUT))
		{
			//保存数据，释放数据
			tea_pdebug("LogindCache::FreeLogoutPlayer %s", it->first.c_str());
			//只有游戏服才需要保存数据
			if(LogindApp::g_app->IsGameServer())
				SavePlayerData(it->first);
			//释放
			ObjMgr.RemovePlayerData(it->first);
			it = m_logoutChar.erase(it);
			continue;
		}
		++it;
	}
}

//添加一个登出缓存玩家
void LogindCache::AddLogoutPlayer(const string &guid)
{
	ASSERT(!guid.empty());
	if(!ObjMgr.Get(guid))//对象管理器里没有，那就不用了
		return;
	auto it = m_logoutChar.find(guid);
	ASSERT(it == m_logoutChar.end());
	m_logoutChar[guid] = time(nullptr);
}

//删除一个登出玩家缓存
void LogindCache::DelLogoutPlayer(const string &guid)
{
	//auto it = m_logoutChar.find(guid);
	//ASSERT(it != m_logoutChar.end());
	m_logoutChar.erase(guid);
}


//玩家自动保存心跳
void LogindCache::Update(logind_player *player)
{
	if(!LogindApp::g_app->IsGameServer())
		return;

	if(!player)
		return;
	time_t t = time(nullptr);
	if(player->GetAutoSaveTime() && player->GetAutoSaveTime() > t)
		return;
	if(player->GetAutoSaveTime() != 0)
		SavePlayerData(player->guid());
	//随机一分钟，让玩家的保存尽量分散
	player->SetAutoSavetime(t + g_Config.auto_save_time + irand(-60,60));
}

//读取对象集合
bool LogindCache::LoadDataSet(const string &guid, vector<GuidObject*> &vec)
{
	m_storage->Goto(g_Config.player_data_hdd_path);
	if(!m_storage->Load(guid, vec))
		return false;
	if(vec.empty())
		return false;
	return true;
}

//读取玩家对象
bool LogindCache::LoadPlayer(const string &guid, logind_player **player, vector<GuidObject*> &vec)
{
	*player = nullptr;
	if(!LoadDataSet(guid, vec))
		return false;
	set<string> guid_set;//去重
	for (auto it = vec.begin(); it != vec.end();)
	{
		GuidObject *obj = *it;
		//去重
		if(guid_set.find(obj->guid()) != guid_set.end())
		{
			it = vec.erase(it);
			safe_delete(obj);
			continue;
		}
		guid_set.insert(obj->guid());
		//查找主玩家
		if(obj->guid() == guid)
		{
			*player = dynamic_cast<logind_player*>(obj);
		}
		++it;
	}
	ASSERT(*player);
	return true;
}

//保存玩家对象
void LogindCache::SavePlayerData(const string &guid)
{
	if(!LogindApp::g_app->IsGameServer())
		return;
	vector<GuidObject*> vec;
	ObjMgr.GetDataSetAllObject(guid, vec);
	logind_player *player = ObjMgr.FindPlayer(guid);
	ASSERT(player);
	SavePlayerData(guid, vec, player->GetLevel());
}

//直接保存数据到硬盘,覆盖文件
bool LogindCache::SaveData(const string &file_name, const string &content)
{
	string file = file_name + BINLOG_EXT_NAME;
	return m_storage->SaveFile(file, content);
}

//通过玩家guid获取系列化字符串
string LogindCache::GetPlayerDataStr(const string &guid)
{
	vector<GuidObject*> vec;
	ObjMgr.GetDataSetAllObject(guid, vec);
	if(vec.empty())
	{
		stringstream ss;
		m_storage->ReadFile(guid + BINLOG_EXT_NAME, [&](const string &txt){
			ss << txt << "\n";
			return true;
		});
		return ss.str();
	}
	return PlayerDataToString(guid, vec);
}

//把玩家数据序列化成字符串
string LogindCache::PlayerDataToString(const string &guid, vector<GuidObject*> &vec)
{
	//把主对象放到第一个
	for (uint32 i = 0; i < vec.size(); i++)
	{
		GuidObject *obj = vec[i];
		if(obj->guid() == guid)
		{
			if(i == 0)
				break;
			vec[i] = vec[0];
			vec[0] = obj;
			break;
		}
	}

	stringstream ss;
	for (auto o:vec)
	{
		string ints,strs;
		o->ToString(ints,strs);
		ss << o->guid() << "\n" << ints << "\n" << strs << "\n";
	}
	return ss.str();
}

//保存玩家对象
void LogindCache::SavePlayerData(const string &guid, vector<GuidObject*> &vec, uint32 level)
{
	if(LogindApp::g_app->m_globalvalue && !LogindApp::g_app->IsGameServer())
		return;
	if(vec.empty())
	{
		tea_pdebug("LogindCache::SavePlayerData vec is empty %s", guid.c_str());
		return;
	}
	string data = PlayerDataToString(guid, vec);
	//开始保存
	m_storage->Goto(g_Config.player_data_hdd_path);
	SaveData(guid, data);

	static char temp[100];
	sprintf(temp, "%s %u\n", guid.c_str(), level);
	string guid_str = temp;
	//保存备份需求
	m_storage->AddContent("backuplist.txt", guid_str);
	//每日保存数据到数据库
	time_t now = time(NULL);
	struct tm *p= localtime(&now);
	int today = (1900 + p->tm_year) * 10000 + (p->tm_mon+1) * 100 + p->tm_mday;
	sprintf(temp,"savetodb_%d.txt", today);
	m_storage->AddContent(temp, guid_str);
}

//备份玩家数据
void LogindCache::Backup(const string &gm_path/* = ""*/)
{
	if(!LogindApp::g_app->IsGameServer())
		return;
	tea_pinfo("LogindCache::Backup begin");
	//从文件中把玩家都读出来
	map<string, uint32> players;
	m_storage->Goto(g_Config.player_data_hdd_path);
	if(!m_storage->ReadFile("backuplist.txt", [&players](const string &txt){
		Tokens tokens;
		StrSplit(tokens, txt, ' ');
		if(tokens.empty())
			return true;
		string guid = tokens[0];
		uint32 level = 0;
		if(tokens.size() >= 2)
			level = atol(tokens[1].c_str());
		players[guid] = level;
		return true;
	}))
	{
		tea_perror("LogindCache::Backup read file fail");
		return;
	}
	//开始备份
	time_t t = time(nullptr);
	struct tm * temp_tm = localtime(&t);
	char temp[50];
	if(gm_path.empty())
		sprintf(temp, "%d%.2d%.2d%.2d%.2d", 1900+temp_tm->tm_year, temp_tm->tm_mon+1, temp_tm->tm_mday, temp_tm->tm_hour, temp_tm->tm_min);
	else
		strncpy(temp, gm_path.c_str(), 49);
	string topath = g_Config.backup_hdd_path + "/" + temp + "/";
	m_storage->Goto(topath);
	string frompath = g_Config.player_data_hdd_path + "/";
	for (auto it:players)
	{
		if(it.first.empty())
			continue;
		string src = frompath + it.first + BINLOG_EXT_NAME;
		string dst = topath + it.first + BINLOG_EXT_NAME;
		if(!m_storage->CopyAFile(src, dst))
			tea_perror("LogindCache::Backup fail , from %s  to %s", src.c_str(), dst.c_str());
		//通知日志服备份下
		WorldPacket pkt (INTERNAL_OPT_SAVE_BACKUP);
		pkt << it.first << temp << it.second;
		LogindApp::g_app->SendToPoliced(pkt);
	}
	//删掉备份txt文件
	m_storage->Remove("backuplist.txt");
	tea_pinfo("LogindCache::Backup end");
}

//保存有变化的玩家到数据库
void LogindCache::SaveChangePlayerToDB(bool is_merge/* = false*/)
{
	if(!LogindApp::g_app->IsGameServer())
		return;

	//如果已经在保存了，就忽略吧
	SavePlayerGuidObj *save_obj = dynamic_cast<SavePlayerGuidObj*>(LogindApp::g_app->m_save_to_db_guid_list);
	ASSERT(save_obj);
	if(save_obj->GetIndex() < save_obj->GetCount())
		return;

	//所有待保存的guid集合
	set<string> guid_set;
	int today;
	if(is_merge)
	{
		//给个很大的值，就都会进入保存了
		today = 999999999;
		//这里因为什么都得保存，所以在线玩家也是要的
		ObjMgr.ForEachPlayer([&guid_set](logind_player *player){
			guid_set.insert(player->guid());
		});
		save_obj->SetPost();
	}
	else
	{
		time_t now = time(NULL);
		struct tm *p= localtime(&now);
		static char temp[50];
		today = (1900 + p->tm_year) * 10000 + (p->tm_mon+1) * 100 + p->tm_mday;
		save_obj->UnSetPost();
	}
	
	vector<string> file_vec;
	//检查一下配置的目录,如果不存在则创建
	m_storage->Goto(g_Config.player_data_hdd_path);
	m_storage->ForEachDir([&](string full_name, string file_name){
		//遍历所有的savetodb前缀文件及savetodb.txt总表
		if(file_name.find("savetodb") == string::npos)
			return;
		bool work = file_name == "savetodb.txt";
		if(!work)
		{
			int i = -1;
			work = (sscanf(file_name.c_str(), "savetodb_%d.txt", &i) > 0 && i > 0 && today > i);
		}
		if(!work)
			return;

		file_vec.push_back(file_name);
		m_storage->ReadFile(file_name, [&](string str){
			Tokens token;
			StrSplit(token, str, ' ');
			if(!token.empty() && !token[0].empty())
				guid_set.insert(token[0]);
			return true;
		});
	});
	//没有，就不用了吧？还是要保存世界变量的
	//if(guid_set.empty())
	//	return;
	//排序
	guid_set.erase(GLOBAL_VALUE_OWNER_STRING);
	//军团
	//guid_set.erase(LEAGUE_BINLOG_OWNER_STRING);
	
	vector<string> vec;
	vector<string> vec_2;
	//合服的时候，是不需要post世界变量的
	if(!is_merge)
	{
		vec.push_back(GLOBAL_VALUE_OWNER_STRING);
		// 军团
		//vec.push_back(LEAGUE_BINLOG_OWNER_STRING);
	}
	//在线的
	for (auto it:guid_set)
	{
		if(ObjMgr.Get(it))
			vec.push_back(it);
		else
			vec_2.push_back(it);
	}
	//不在线的
	for (auto it:vec_2)
	{
		string guid_server_name = MongoDB::GetServerName(it);
		if(!LogindApp::g_app->IsMyServer(guid_server_name))
		{
			tea_pinfo("LogindCache::SaveChangePlayerToDB not local data, %s", it.c_str());
			continue;
		}
		vec.push_back(it);
	}
	
	save_obj->SetToday(today);
	uint32 i = 0;
	for (auto it:vec)
	{
		save_obj->SetSavePlayerGuid(i, it);
		i++;
	}
	save_obj->SetCount(i);
	save_obj->SetIndex(0);

	//删除文件
	for (auto it:file_vec)
	{
		m_storage->Remove(it);
	}
}

//从本地硬盘把所有账号信息读出来
void LogindCache::LoadAllAccountInfo()
{
	m_storage->Goto(g_Config.player_data_hdd_path);
	stringstream ss;
	bool first_line = true;
	bool new_version = false;
	string account_file = g_Config.player_data_hdd_path + "/account.txt";
	if(!core_obj::Storage::IsHaveFile(account_file))
	{
		m_storage->AddContent("account.txt", "new version\n");
		return;
	}

	if(!m_storage->ReadFile("account.txt", [&](const string &str){
		if(first_line)
		{
			new_version = (str == "new version");
			first_line = false;
			return new_version;
		}

		account_table *info = new account_table;
		info->NewFromString(str);
		AddAccount(info);
		return true;
	}))
	{
		tea_perror("LogindCache::LoadAllAccountInfo read  txt file fail");
		return;
	}

	//如果不是新版本的文件，删掉重来
	if(!new_version)
	{
		m_storage->Remove("account.txt");
		m_storage->AddContent("account.txt", "new version\n");
	}
}

//保存账号信息到硬盘中
void LogindCache::SaveAccountInfo(account_table *info)
{
	ASSERT(info);
	string str = info->ToString();
	m_storage->Goto(g_Config.player_data_hdd_path);
	m_storage->AddContent("account.txt", str + "\n");
}

//从本地硬盘把所有账号角色信息读出来
void LogindCache::LoadAllAccountCharInfo()
{
	m_storage->Goto(g_Config.player_data_hdd_path);
	stringstream ss;
	if(!m_storage->ReadFile("account_char.txt", [&](const string &str){
		Tokens tokens;
		StrSplit(tokens, str, ' ');		
		AddAccountToChar(tokens[0], tokens[1]);
		return true;
	}))
	{
		tea_perror("LogindCache::LoadAllAccountInfo read  txt file fail");
		return;
	}
	////去重
	//m_storage->Remove("account_char.txt");
	//for (auto it:m_charListHD)
	//{
	//	SaveAccountCharInfo(it.first, it.second.guid);
	//}
}

//保存账号角色信息到硬盘中
void LogindCache::SaveAccountCharInfo(const string& account,const string& guid)
{
	m_storage->Goto(g_Config.player_data_hdd_path);
	m_storage->AddContent("account_char.txt", account + ' ' + guid + '\n');
}

//回档指定玩家
bool LogindCache::HuidangPlayerInfos(const string player_guid, const string backupfilename)
{
	string path = g_Config.backup_hdd_path + "/" + backupfilename + "/";
	m_storage->Goto(path);

	OwnerDataSet* dataset = ObjMgr.FindDataSetByOwnerGuid(player_guid);	
	if(dataset && !dataset->GetAllData().empty())
	{
		//玩家数据在线
		vector<GuidObject*> vec;
		if(!m_storage->Load(player_guid, vec))
		{
			tea_pinfo("LogindCache::HuidangPlayerInfos load player %s data from %s failed!", player_guid.c_str(), path.c_str());
			return false;
		}
		vector<GuidObject*> need_free_vec;	//需要释放的对象
		vector<GuidObject*> off_vec;		//需要为玩家添加上的对象
		vector<string> off_guid_vec;		//需要为玩家添加上的对象的guid
		map<string, GuidObject*> data_map;	//专成map以查找需要移除的对象
		for (auto it:vec)
		{
			//只认第一个重复数据
			if(data_map.find(it->guid()) != data_map.end())
			{
				need_free_vec.push_back(it);
				continue;
			}
			data_map[it->guid()] = it;

			GuidObject *obj = ObjMgr.Get(it->guid());
			if(obj)
			{
				obj->SetBinlogMaxSize(SyncEventRecorder::MAX_BINLOG_SIZE_UNLIME);
				uint32 len = obj->length_uint32() > it->length_uint32() ? obj->length_uint32() : it->length_uint32();
				for (uint32 i = 0; i < len; i++)
				{
					if(obj->GetUInt32(i) != it->GetUInt32(i))
					{
						obj->SetUInt32(i, it->GetUInt32(i));
					}
				}
				len = obj->length_str() > it->length_str() ? obj->length_str() : it->length_str();
				for (uint32 i = 0; i < len; i++)
				{
					if(obj->GetStr(i) != it->GetStr(i))
					{
						obj->SetStr(i, it->GetStr(i));
					}
				}
				obj->SetBinlogMaxSize(SyncEventRecorder::MAX_BINLOG_SIZE_2);
				need_free_vec.push_back(it);
			}
			else
			{
				off_vec.push_back(it);
				off_guid_vec.push_back(it->guid());
			}
		}
		//移除不存在的 begin
		set<string> all_data;
		for (auto it:dataset->GetAllData())
		{
			all_data.insert(it);
		}
		for(auto it: all_data)
		{
			if(data_map.find(it) == data_map.end())
			{
				ObjMgr.CallRemoveObject(it);
			}
		}
		//移除不存在的 end
		//需要添加的对象
		if(!off_vec.empty())
		{
			ObjMgr.CallPutsObject(player_guid, off_vec, [off_guid_vec](bool b){
				//把binlog的拥有者管理器弄一下
				for (auto it:off_guid_vec)
				{
					//todo jzy  这一句，是为了糊datad版本不兼容的问题
					ObjMgr.CallAddWatch(it, nullptr, false);
					ObjMgr.InsertObjOwner(it);
				}
			});
		}
		//需要释放的对象
		for (auto it:need_free_vec)
		{
			safe_delete(it);
		}
	}
	else
	{
		//玩家数据不在线,直接拷贝就好
		string src = path + player_guid + BINLOG_EXT_NAME;
		string dst = g_Config.player_data_hdd_path + "/" + player_guid + BINLOG_EXT_NAME;
		m_storage->CopyAFile(src, dst);
	}

	return true;
}

//读取世界变量对象
bool LogindCache::LoadGlobalValue()
{
	vector<GuidObject*> vec;
	return LoadDataSet(GLOBAL_VALUE_OWNER_STRING, vec);
}

//读取军团变量对象
bool LogindCache::LoadFractionValue()
{
	vector<GuidObject*> vec;
	if (!LoadDataSet(FACTION_BINLOG_OWNER_STRING, vec))
		return false;
	
	vector<string> guid_vec;
	for (auto it : vec) {
		guid_vec.push_back(it->guid());
	}

	ObjMgr.CallPutsObject(FACTION_BINLOG_OWNER_STRING, vec, [guid_vec](bool) {
		for (auto it : guid_vec) {
			ObjMgr.InsertObjOwner(it);
		}
	});

	return true;
}

//读取军团数据
bool LogindCache::LoadFractionData()
{
	vector<GuidObject*> vec;
	if (!LoadDataSet(FACTION_DATA_OWNER_STRING, vec))
		return false;

	vector<string> guid_vec;
	for (auto it : vec) {
		guid_vec.push_back(it->guid());
	}

	ObjMgr.CallPutsObject(FACTION_DATA_OWNER_STRING, vec, [guid_vec](bool) {
		for (auto it : guid_vec) {
			ObjMgr.InsertObjOwner(it);
		}
	});

	return true;
}

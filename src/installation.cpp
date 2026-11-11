#include "installation.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace ta {
namespace {
std::wstring env(const wchar_t* name){DWORD n=GetEnvironmentVariableW(name,nullptr,0);if(!n)return {};std::wstring s(n,L'\0');GetEnvironmentVariableW(name,s.data(),n);s.resize(n-1);return s;}
std::wstring lower(std::wstring s){for(auto& c:s)c=towlower(c);return s;}
std::wstring expand(const std::wstring& s){DWORD n=ExpandEnvironmentStringsW(s.c_str(),nullptr,0);if(!n||n>32768)return {};std::wstring out(n,L'\0');ExpandEnvironmentStringsW(s.c_str(),out.data(),n);out.resize(n-1);return out;}
std::wstring registryValue(HKEY key,const wchar_t* name){DWORD type=0,n=0;if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&n)!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||n>65536)return {};std::wstring s(n/sizeof(wchar_t)+1,L'\0');if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(s.data()),&n)!=ERROR_SUCCESS)return {};s.resize(wcsnlen(s.data(),s.size()));return expand(s);}
using Candidates=std::vector<Installation>;
void add(Candidates& out,const fs::path& p,const std::string& source){if(!p.empty())out.push_back({p,source});}
void addFolder(Candidates& out,const fs::path& p,const std::string& source){if(p.empty())return;for(auto tail:{L"TeleAgent.exe",L"TeleAgent/TeleAgent.exe",L"TeleClaw/TeleAgent.exe",L"TeleClaw/TeleAgent/TeleAgent.exe",L"bin/TeleAgent.exe",L"current/TeleAgent.exe"})add(out,p/tail,source);}
bool sameUser(HANDLE process){HANDLE a=nullptr,b=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&a))return false;if(!OpenProcessToken(process,TOKEN_QUERY,&b)){CloseHandle(a);return false;}DWORD na=0,nb=0;GetTokenInformation(a,TokenUser,nullptr,0,&na);GetTokenInformation(b,TokenUser,nullptr,0,&nb);std::vector<BYTE> ua(na),ub(nb);bool ok=na&&nb&&GetTokenInformation(a,TokenUser,ua.data(),na,&na)&&GetTokenInformation(b,TokenUser,ub.data(),nb,&nb)&&EqualSid(reinterpret_cast<TOKEN_USER*>(ua.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(ub.data())->User.Sid);CloseHandle(a);CloseHandle(b);return ok;}
void running(Candidates& out){HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return;PROCESSENTRY32W e{};e.dwSize=sizeof(e);DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);for(BOOL ok=Process32FirstW(snapshot,&e);ok;ok=Process32NextW(snapshot,&e)){DWORD other=0;if(_wcsicmp(e.szExeFile,L"TeleAgent.exe")||!ProcessIdToSessionId(e.th32ProcessID,&other)||other!=session)continue;HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID);if(p){wchar_t path[32768];DWORD n=32768;if(sameUser(p)&&QueryFullProcessImageNameW(p,0,path,&n))add(out,fs::path(std::wstring(path,n)),"运行中的 TeleAgent");CloseHandle(p);}}CloseHandle(snapshot);}
void registry(Candidates& out){for(auto root:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
    HKEY key=nullptr;if(RegOpenKeyExW(root,L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\TeleAgent.exe",0,KEY_READ|view,&key)==ERROR_SUCCESS){add(out,executableFromValue(registryValue(key,nullptr)),"系统安装记录");RegCloseKey(key);}
    if(RegOpenKeyExW(root,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&key)!=ERROR_SUCCESS)continue;
    for(DWORD i=0;;++i){wchar_t name[512];DWORD n=512;auto status=RegEnumKeyExW(key,i,name,&n,nullptr,nullptr,nullptr,nullptr);if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)continue;HKEY item=nullptr;if(RegOpenKeyExW(key,name,0,KEY_READ|view,&item)!=ERROR_SUCCESS)continue;auto display=lower(registryValue(item,L"DisplayName"));if(display.find(L"teleagent")!=std::wstring::npos||display.find(L"teleclaw")!=std::wstring::npos){add(out,executableFromValue(registryValue(item,L"DisplayIcon")),"系统安装记录");auto folder=registryValue(item,L"InstallLocation");if(!folder.empty())addFolder(out,fs::path(folder),"系统安装记录");}RegCloseKey(item);}RegCloseKey(key);
}}
void shortcuts(Candidates& out){HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return;
    for(int folder:{CSIDL_PROGRAMS,CSIDL_COMMON_PROGRAMS,CSIDL_DESKTOPDIRECTORY,CSIDL_COMMON_DESKTOPDIRECTORY}){wchar_t path[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,folder,nullptr,SHGFP_TYPE_CURRENT,path)))continue;std::error_code ec;fs::recursive_directory_iterator it(path,fs::directory_options::skip_permission_denied,ec),end;for(;it!=end;it.increment(ec)){if(ec){ec.clear();continue;}if(it.depth()>4){it.disable_recursion_pending();continue;}if(lower(it->path().extension().wstring())!=L".lnk")continue;IShellLinkW* link=nullptr;if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))))continue;IPersistFile* file=nullptr;if(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file)))){if(SUCCEEDED(file->Load(it->path().c_str(),STGM_READ))){wchar_t target[32768]{};if(SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH)))add(out,fs::path(expand(target)),"开始菜单或桌面快捷方式");}file->Release();}link->Release();}}
    if(SUCCEEDED(hr))CoUninitialize();
}
Installation firstValid(const Candidates& candidates){for(auto& c:candidates)if(validExecutable(c.exe))return {fs::absolute(c.exe).lexically_normal(),c.source};return {};}
}
bool validExecutable(const fs::path& path){try{if(path.empty()||!path.is_absolute()||lower(path.filename().wstring())!=L"teleagent.exe"||!fs::is_regular_file(path))return false;std::ifstream file(path,std::ios::binary);IMAGE_DOS_HEADER dos{};file.read(reinterpret_cast<char*>(&dos),sizeof(dos));if(!file||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<static_cast<LONG>(sizeof(dos))||dos.e_lfanew>1024*1024)return false;file.seekg(dos.e_lfanew);DWORD signature=0;IMAGE_FILE_HEADER header{};file.read(reinterpret_cast<char*>(&signature),sizeof(signature));file.read(reinterpret_cast<char*>(&header),sizeof(header));return file&&signature==IMAGE_NT_SIGNATURE&&(header.Characteristics&IMAGE_FILE_EXECUTABLE_IMAGE)&&!(header.Characteristics&IMAGE_FILE_DLL);}catch(...){return false;}}
fs::path executableFromValue(const std::wstring& value){auto s=expand(value);auto a=s.find_first_not_of(L" \t\r\n");if(a==std::wstring::npos)return {};s=s.substr(a);if(s.front()==L'"'){auto end=s.find(L'"',1);if(end==std::wstring::npos)return {};s=s.substr(1,end-1);}else{auto end=lower(s).find(L".exe");if(end==std::wstring::npos)return {};s.resize(end+4);}return fs::path(s);}
Installation findInstallationInFolder(const fs::path& folder){if(folder.empty())return {};Candidates list;addFolder(list,fs::absolute(folder),"手动选择的安装目录");return firstValid(list);}
Installation findInstallation(const fs::path& state,bool useSaved){Candidates list;
    try{auto saved=loadJson(state/L"settings.json")["teleagentExecutable"].str();if(useSaved&&!saved.empty())add(list,fs::path(wide(saved)),"手动选择并保存的路径");}catch(...){}
    auto found=firstValid(list);if(!found.exe.empty())return found;list.clear();running(list);registry(list);
    for(auto variable:{L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"}){auto folder=env(variable);if(!folder.empty())addFolder(list,fs::path(folder),"常见安装目录");}
    auto local=env(L"LOCALAPPDATA");if(!local.empty()){addFolder(list,fs::path(local)/L"Programs","当前用户安装目录");addFolder(list,fs::path(local)/L"TeleAgent","当前用户安装目录");}
    found=firstValid(list);if(!found.exe.empty())return found;shortcuts(list);return firstValid(list);
}
void rememberInstallation(const fs::path& state,const fs::path& exe){if(!validExecutable(exe))throw std::runtime_error("Select a valid TeleAgent.exe executable");privateDirectory(state);auto settings=loadJson(state/L"settings.json");settings["teleagentExecutable"]=utf8(fs::absolute(exe).lexically_normal().wstring());writeAtomic(state/L"settings.json",settings.dump());}
fs::path savedDataRoot(const fs::path& state){try{auto value=loadJson(state/L"settings.json")["teleAgentDataRoot"].str();if(!value.empty())return fs::path(wide(value));}catch(...){}return {};}
void rememberDataRoot(const fs::path& state,const fs::path& root){privateDirectory(state);auto settings=loadJson(state/L"settings.json");settings["teleAgentDataRoot"]=utf8(fs::absolute(root).lexically_normal().wstring());writeAtomic(state/L"settings.json",settings.dump());}
int accountCount(const fs::path& root){
    int count=0;
    try{std::error_code ec;for(fs::directory_iterator it(root/L"users",fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){if(ec){ec.clear();continue;}std::string name;std::error_code dir;try{name=utf8(it->path().filename().wstring());}catch(...){continue;}if(name.rfind("v1_public_",0)==0&&it->is_directory(dir))++count;}}catch(...){}
    return count;
}
namespace {
bool looksLikeDataRoot(const fs::path& p){
    std::error_code ec;
    if(!fs::is_directory(p,ec))return false;
    return fs::is_directory(p/L"users",ec)||fs::exists(p/L"current-owner.json",ec)||(fs::is_directory(p/L"Partitions",ec)&&fs::is_directory(p/L"app-auth",ec));
}
}
// TeleAgent is an Electron app that may keep its data outside %USERPROFILE%:
// portable installs, a moved profile, XDG_DATA_HOME or a different drive. Try
// every plausible location, keep a manually saved directory when it still
// works, and otherwise take the candidate with the most accounts.
DataRoot findDataRoot(const fs::path& state,const fs::path& exe,bool useSaved){
    std::vector<std::pair<fs::path,std::string>> candidates;
    auto add=[&](const fs::path& p,const char* source){if(p.empty())return;auto normal=fs::absolute(p).lexically_normal();for(auto& c:candidates)if(c.first==normal)return;candidates.emplace_back(normal,source);};
    auto saved=useSaved?savedDataRoot(state):fs::path{};if(!saved.empty())add(saved,"手动选择并保存的目录");
    auto home=env(L"USERPROFILE");
    add(fs::path(home)/L".local"/L"share"/L"TeleAgent","默认位置");
    auto xdg=env(L"XDG_DATA_HOME");if(!xdg.empty())add(fs::path(xdg)/L"TeleAgent","XDG 数据目录");
    auto other=env(L"HOME");if(!other.empty())add(fs::path(other)/L".local"/L"share"/L"TeleAgent","用户目录");
    auto appdata=env(L"APPDATA");if(!appdata.empty()){add(fs::path(appdata)/L"TeleAgent","漫游数据目录");add(fs::path(appdata)/L"teleagent","漫游数据目录");}
    auto local=env(L"LOCALAPPDATA");if(!local.empty()){add(fs::path(local)/L"TeleAgent","本机数据目录");add(fs::path(local)/L"Programs"/L"TeleAgent","当前用户安装目录");}
    add(fs::path(home)/L"TeleAgent","用户目录");
    auto programData=env(L"ProgramData");if(!programData.empty())add(fs::path(programData)/L"TeleAgent","公共数据目录");
    if(!exe.empty()){auto dir=exe.parent_path();add(dir,"TeleAgent 程序目录");add(dir/L"data","TeleAgent 程序目录");add(dir/L"TeleAgent","TeleAgent 程序目录");add(dir.parent_path()/L"TeleAgent","TeleAgent 程序上级目录");add(dir.parent_path()/L"data","TeleAgent 程序上级目录");}
    if(!saved.empty()&&looksLikeDataRoot(saved))return {saved,"手动选择并保存的目录",true,accountCount(saved)};
    DataRoot best;int bestScore=-1;
    for(auto& candidate:candidates){
        if(!looksLikeDataRoot(candidate.first))continue;
        int score=accountCount(candidate.first);
        if(score>bestScore){bestScore=score;best={candidate.first,candidate.second,true,score};}
    }
    if(best.valid)return best;
    auto fallback=saved.empty()?fs::path(home)/L".local"/L"share"/L"TeleAgent":saved;
    return {fallback,saved.empty()?"默认位置":"手动选择并保存的目录",false,0};
}
fs::path userStateDirectory(){wchar_t path[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path)))throw std::runtime_error("Cannot find Windows local application data");return fs::path(path)/L"TeleAgentAccountDesk"/L"manager-data";}
int installationSelfTest(const fs::path& directory){privateDirectory(directory);auto work=directory/L"installation-fixture";privateDirectory(work);Json::Array results;int failed=0;auto test=[&](const char* name,auto f){bool ok=f();if(!ok)++failed;results.emplace_back(Json::Object{{"test",name},{"passed",ok}});};wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);auto exe=fs::absolute(work/L"自定义 路径"/L"TeleAgent.exe");privateDirectory(exe.parent_path());fs::copy_file(self,exe,fs::copy_options::overwrite_existing);
    test("Quoted registry icon path with spaces",[&]{return executableFromValue(L"\""+exe.wstring()+L"\",0")==exe;});
    test("Unquoted registry icon path with spaces",[&]{return executableFromValue(exe.wstring()+L",0")==exe;});
    test("Executable validation rejects missing and non-PE files",[&]{writeAtomic(work/L"TeleAgent.exe","not an executable");return validExecutable(exe)&&!validExecutable(work/L"TeleAgent.exe")&&!validExecutable(work/L"missing.exe")&&!validExecutable(fs::path(L"TeleAgent.exe"));});
    test("Saved custom installation is discovered",[&]{rememberInstallation(work,exe);return findInstallation(work).exe==exe;});
    test("Account data root detection counts owner directories",[&]{auto root=work/L"data-root";privateDirectory(root);privateDirectory(root/L"users"/L"v1_public_1001");privateDirectory(root/L"users"/L"v1_public_1002");privateDirectory(root/L"users"/L"__device_login__");privateDirectory(work/L"partitions-only"/L"Partitions");privateDirectory(work/L"partitions-only"/L"app-auth");privateDirectory(work/L"empty");return accountCount(root)==2&&looksLikeDataRoot(root)&&looksLikeDataRoot(work/L"partitions-only")&&!looksLikeDataRoot(work/L"empty")&&!looksLikeDataRoot(work/L"missing-root");});
    test("Installation folder resolves nested TeleClaw and rejects empty folders",[&]{
        auto folder=work/L"另一磁盘 中文 空格";auto nested=folder/L"TeleClaw"/L"TeleAgent.exe";
        privateDirectory(nested.parent_path());fs::copy_file(self,nested,fs::copy_options::overwrite_existing);
        return findInstallationInFolder(folder).exe==fs::absolute(nested)&&findInstallationInFolder(work/L"empty").exe.empty();
    });
    test("Saved data root is honored but explicit rescan bypasses it",[&]{
        // Redirect every environment-based candidate so this test never reads real account data.
        struct Environment {
            std::vector<std::pair<std::wstring,std::wstring>> values;
            ~Environment(){for(auto& v:values)SetEnvironmentVariableW(v.first.c_str(),v.second.empty()?nullptr:v.second.c_str());}
        } restore;
        for(auto name:{L"USERPROFILE",L"HOME",L"XDG_DATA_HOME",L"APPDATA",L"LOCALAPPDATA",L"ProgramData"}){
            restore.values.emplace_back(name,env(name));SetEnvironmentVariableW(name,(work/L"isolated-home").c_str());
        }
        auto state=work/L"root-settings";auto old=work/L"old-root";
        privateDirectory(old/L"users"/L"v1_public_1001");rememberDataRoot(state,old);
        auto portable=work/L"portable";privateDirectory(portable/L"users"/L"v1_public_1002");
        auto app=portable/L"TeleAgent.exe";
        auto saved=findDataRoot(state,app);auto rescanned=findDataRoot(state,app,false);
        return saved.path==fs::absolute(old)&&rescanned.valid&&rescanned.path==fs::absolute(portable)&&rescanned.accounts==1;
    });
    test("Stale candidate falls through to another installation",[&]{return firstValid({{work/L"missing"/L"TeleAgent.exe","old"},{exe,"custom"}}).exe==exe;});
    test("Invalid manual selection preserves previous configuration",[&]{auto before=readFile(work/L"settings.json");try{rememberInstallation(work,work/L"TeleAgent.exe");return false;}catch(...){return readFile(work/L"settings.json")==before;}});
    writeAtomic(directory/L"installation-tests.json",Json(Json::Object{{"failed",Json::num(failed)},{"tests",results}}).dump());return failed?2:0;
}
}

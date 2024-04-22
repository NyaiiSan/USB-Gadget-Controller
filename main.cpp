#include <iostream>
#include <fstream>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <usbg/usbg.h>

#define ID_VENDOR 0x0000
#define ID_PRODUCT 0x0000

using namespace std;

class Ugc_conf{

public:
    char serial[9]; // 保存设备串号
    usbg_gadget_attrs *usbg_gca; // USB gadget device attributes
    usbg_gadget_strs *usbg_gcs; // USB gadget device strings

    Ugc_conf(uint16_t, uint16_t);
    ~Ugc_conf();
};

Ugc_conf::Ugc_conf(uint16_t idVendor, uint16_t idProduct)
{
    struct utsname uname_s; // 保存设备信息
    uname(&uname_s); // 获取设备信息
    
    // 获取设备id
    ifstream ifs; // 设备id文件流
    ifs.open("/etc/machine-id");
    string mec_id;
    ifs >> mec_id;
    memset(serial, 0, 9);
    strncpy(serial, mec_id.c_str(), 8); // 区设备id前8位作为设备串号
    
    // 初始化Config
    usbg_gca = (usbg_gadget_attrs *)malloc(sizeof(usbg_gadget_attrs));
    usbg_gcs = (usbg_gadget_strs *)malloc(sizeof(usbg_gadget_strs));
    usbg_gca->bcdUSB = 0x0200;
    usbg_gca->bDeviceClass = 0x00;
    usbg_gca->bDeviceSubClass = 0x00;
    usbg_gca->bDeviceProtocol = 0x00;
    usbg_gca->bMaxPacketSize0 = 64;
    usbg_gca->idVendor = idVendor;
    usbg_gca->idProduct = idProduct;
    usbg_gca->bcdDevice = 0x0000;
    usbg_gcs->manufacturer = uname_s.sysname;
    usbg_gcs->product = uname_s.nodename;
    usbg_gcs->serial = serial;
}
Ugc_conf::~Ugc_conf()
{
    free(usbg_gca);
    free(usbg_gcs);
}

// 定义Gadget类
class Gadget{

private:
    int init_gadget();
    int get_fconfig(usbg_gadget *, usbg_config **);
public:
    char configfs_path[128]; // 配置文件系统的路径
    char conf_name[32];
    Ugc_conf *conf;
    string name;  // Gadget的名字
    usbg_state *usbg_s; // State of the gadget devices in the system
    usbg_gadget *usbg_g;

    Gadget(string, string, Ugc_conf *);
    int add_function(string);
    int enable();
    int disable();
};

Gadget::Gadget(string name_g, string parh, Ugc_conf *conf_p){
    name = name_g;
    strcpy(configfs_path, parh.c_str());
    conf = conf_p;
    strcpy(conf_name, "conf");
}

int Gadget::init_gadget(){
    // 初始化
    int res = usbg_init(configfs_path, &usbg_s);
    if(res != USBG_SUCCESS){
        cerr << "Init State Error!" << endl;
        usbg_s = NULL;
        return res;
    }
    // 判断是否已经存在gadget
    usbg_g = usbg_get_first_gadget(usbg_s);
    if(usbg_g == NULL){
        // 不存在则创建设备
        res = usbg_create_gadget(usbg_s, name.c_str(), conf->usbg_gca, conf->usbg_gcs, &usbg_g);
        if(res != USBG_SUCCESS){
            cerr << "Create gadget Error !" << endl;
            usbg_g = NULL;
            return res;
        }
    }
    usbg_disable_gadget(usbg_g);

    return USBG_SUCCESS;
}

// 获取一个usbg_config
int Gadget::get_fconfig(usbg_gadget *usbg_g, usbg_config **usbg_c){

    struct usbg_config_strs usbg_conf_strs = {
        .configuration = conf_name
    };

    *usbg_c = usbg_get_first_config(usbg_g);
    if(*usbg_c == NULL){
        int res = usbg_create_config(usbg_g, 1, conf_name, NULL, &usbg_conf_strs, usbg_c);
        if(res != USBG_SUCCESS){
            cerr << "Get usbg_config Error!" << endl;
            *usbg_c = NULL;
            return res;
        }
    }
    return USBG_SUCCESS;
}

// 向 Gadget 添加一个功能
int Gadget::add_function(string type){

    int res = init_gadget();
    if(res != USBG_SUCCESS){
        return res;
    }

    usbg_function_type ftype;
    char usbg_fun_n[32];
    if(type == "adb"){ // 创建一个adb
        ftype = USBG_F_FFS;
        strcpy(usbg_fun_n, "adb");
    }
    else if(type == "ffs"){ // 创建一个ffs
        ftype = USBG_F_FFS;
        strcpy(usbg_fun_n, "ugc_fun_ffs");
    }
    else if(type == "rndis"){ // 创建一个rndis
        ftype = USBG_F_RNDIS;
        strcpy(usbg_fun_n, "ugc_fun_rndis");
    }
    else{
        cerr << "Function name not found!" << endl;
        return USBG_ERROR_NOT_FOUND;
    }

    usbg_function *usbg_f;

    res = usbg_create_function(usbg_g, ftype, usbg_fun_n, NULL, &usbg_f);
    if(res != 0){
        cerr << "Create function Error!" << endl;
        usbg_cleanup(usbg_s);
        return res;
    }

    usbg_config *usbg_c = NULL;
    res = get_fconfig(usbg_g, &usbg_c);
    if(res != USBG_SUCCESS){
        cerr << "Get Config error !" << endl;
        return res;
    }

    res = usbg_add_config_function(usbg_c, usbg_fun_n, usbg_f);
    if(res != 0){
        cerr << "Config function error !" << endl;
        usbg_cleanup(usbg_s);
        return res;
    }

    usbg_cleanup(usbg_s);
    return USBG_SUCCESS;
}

// 开启 Gadget
int Gadget::enable(){
    int res = init_gadget();
    if(res != USBG_SUCCESS){
        return res;
    }

    res = usbg_enable_gadget(usbg_g, DEFAULT_UDC);
    if(res != USBG_SUCCESS){
        return res;
    }
    return USBG_SUCCESS;
}

// 禁用 Gadget
int Gadget::disable(){
    int res = init_gadget();
    if(res != USBG_SUCCESS){
        return res;
    }

    res = usbg_disable_gadget(usbg_g);
    if(res != USBG_SUCCESS){
        return res;
    }
    return USBG_SUCCESS;
}

int main(int argc, char *argv[]){

    Ugc_conf conf(ID_VENDOR, ID_PRODUCT);

    Gadget ugc("ugc", "/sys/kernel/config", &conf);

    if(argc < 2){
        printf("Error argv! \n");
        return USBG_ERROR_INVALID_PARAM;
    }

    if(strcmp("start", argv[1]) == 0){ // 启动gadget
        int res = ugc.enable();
        if(res != USBG_SUCCESS){
            cerr << "Gadget start Error !" << res << endl;
            return res;
        }
    }
    else if(strcmp("stop", argv[1]) == 0){ // 关闭gadget
        int res = ugc.disable();
        if(res != USBG_SUCCESS){
            cerr << "Gadget stop Error !" << res << endl;
            return res;
        }
    }
    else if(strcmp("add", argv[1]) == 0){
        
        if(argc < 3){
            printf("Error argv! \n");
            return USBG_ERROR_INVALID_PARAM;
        }

        if (strcmp("add", argv[1]) == 0){ // 添加一个功能
            int res = ugc.add_function(argv[2]);
            if(res != USBG_SUCCESS){
                cout << "Add Function Error!" << endl;
                return res;
            }
        }
    }
    
    return USBG_SUCCESS;
}
﻿#include <sclog.h>
#include <scatom.h>
#include <pthread.h>

//-------------------------------------------------------------------------------------------|
// 第二部分 对日志信息体操作的get和set,这里隐藏了信息体的实现
//-------------------------------------------------------------------------------------------|

static struct {
	pthread_key_t	key;	//全局线程私有变量
	pthread_once_t	once;	//全局初始化用的类型
	unsigned		logid;	//默认的全局logid, 唯一标识
	FILE *			log;	//log文件指针
	FILE *			wf;		//wf文件指针
} _slmain = { (pthread_key_t)0, PTHREAD_ONCE_INIT, 0, NULL, NULL };

//内部简单的释放函数,服务于pthread_key_create 防止线程资源泄露
static void _slinfo_destroy(void * slinfo) {
	free(slinfo);
}

static void _gkey(void) {
	pthread_key_create(&_slmain.key, _slinfo_destroy);
}

struct slinfo {
	unsigned		logid;					//请求的logid,唯一id
	char			reqip[_INT_LITTLE];		//请求方ip
	char			times[_INT_LITTLE];		//当前时间串
	struct timespec	timec;					//处理时间,保存值,统一用毫秒, 精确到纳秒
	char			mod[_INT_LITTLE];		//当前线程的模块名称,不能超过_INT_LITTLE - 1
};

/**
*	线程的私有数据初始化
**
** mod		: 当前线程名称
** reqip	: 请求的ip
** return	: Success_Base 表示正常, Error_Alloc内存分配错误
**/
int
sl_init(const char mod[_INT_LITTLE], const char reqip[_INT_LITTLE]) {
	struct slinfo * pl;

	//保证 _gkey只被执行一次
	pthread_once(&_slmain.once, _gkey);

	if ((pl = pthread_getspecific(_slmain.key)) == NULL) {
		//重新构建
		if ((pl = malloc(sizeof(struct slinfo))) == NULL)
			return Error_Alloc;
	}

	timespec_get(&pl->timec, TIME_UTC);
	//设置日志logid, 有设置, 没有默认原子自增
	pl->logid = ATOM_INC(_slmain.logid);
	strncpy(pl->mod, mod, _INT_LITTLE); //复制一些数据
	strncpy(pl->reqip, reqip, _INT_LITTLE);

	//设置私有变量
	pthread_setspecific(_slmain.key, pl);

	return Success_Base;
}

/**
*	获取日志信息体的唯一的logid
**/
inline unsigned
sl_getlogid(void) {
	struct slinfo * pl = pthread_getspecific(_slmain.key);
	return pl ? pl->logid : 0u;
}

/**
*	获取日志信息体的请求ip串,返回NULL表示没有初始化
**/
inline const char *
sl_getreqip(void) {
	struct slinfo * pl = pthread_getspecific(_slmain.key);
	return pl ? pl->reqip : NULL;
}

/**
*	获取日志信息体的名称,返回NULL表示没有初始化
**/
inline const char *
sl_getmod(void) {
	struct slinfo * pl = pthread_getspecific(_slmain.key);
	return pl ? pl->mod : NULL;
}

//-------------------------------------------------------------------------------------------|
// 第三部分 对日志系统具体的输出输入接口部分
//-------------------------------------------------------------------------------------------|

/**
*	日志系统首次使用初始化,找对对映日志文件路径,创建指定路径
**/
void
sl_start(void) {
	// 主线程调用, 只会启动一次
	if (!_slmain.log && !_slmain.wf) {
		// 这里可以单独开启一个线程或进程,处理日志整理但是 这个模块可以让运维做,按照规则搞
		sl_init("main thread", "0.0.0.0");
	}

	// 单例只执行一次, 打开普通log文件
	if (NULL == _slmain.log) {
		_slmain.log = fopen(_STR_SCLOG_LOG, "a+");
		if (NULL == _slmain.log)
			CERR_EXIT("_slmain.log fopen %s error!", _STR_SCLOG_LOG);
	}

	// 继续打开 wf 文件
	if (NULL == _slmain.wf) {
		_slmain.wf = fopen(_STR_SCLOG_WFLOG, "a+");
		if (!_slmain.wf) {
			fclose(_slmain.log); // 其实这都没有必要,图个心安
			CERR_EXIT("_slmain.log fopen %s error!", _STR_SCLOG_WFLOG);
		}
	}

	// 释放工作交给操作系统
}

/**
*	获取日志信息体的时间串,返回NULL表示没有初始化
**/
static const char * _sl_gettimes(void) {
	time_t td;
	struct timespec et; //记录时间

	struct slinfo * pl = pthread_getspecific(_slmain.key);
	if (NULL == pl) //返回NULL表示没有找见
		return NULL;

	timespec_get(&et, TIME_UTC);
	//同一用毫秒记
	td = _INT_STOMS * (et.tv_sec - pl->timec.tv_sec) + (et.tv_nsec - pl->timec.tv_nsec) / _INT_MSTONS;
	snprintf(pl->times, sizeof(pl->times), "%"PRId64, td);

	return pl->times;
}

/**
*	这个函数不希望你使用,是一个内部限定死的日志输出内容.推荐使用相应的宏
** 打印相应级别的日志到对映的文件中.
**
**	format		: 必须是""号括起来的宏,开头必须是 [FALTAL:%s]后端错误
**				[WARNING:%s]前端错误, [NOTICE:%s]系统使用, [INFO:%s]普通信息,
**				[DEBUG:%s] 开发测试用
**
** return		: 返回操作结果
**/
void
sl_printf(const char * format, ...) {
	int len;
	va_list ap;
	stime_t tstr;
	char str[_UINT_LOGS];

	//初始化时间参数
	stu_getntstr(tstr);
	len = snprintf(str, sizeof(str), "[%s %s]", tstr, _sl_gettimes());

	va_start(ap, format);
	vsnprintf(str + len, sizeof(str) - len, format, ap);
	va_end(ap);

	// 写普通文件 log
	fputs(str, _slmain.log); //把锁机制去掉了,fputs就是线程安全的

	// 写警告文件 wf
	if (format[1] == 'F' || format[1] == 'W') //当为FATAL或WARNING需要些写入到警告文件中
		fputs(str, _slmain.wf);
}
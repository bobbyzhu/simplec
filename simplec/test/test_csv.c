﻿#include <schead.h>
#include <sclog.h>
#include <sccsv.h>

#define _STR_PATH "test/config/onetime.csv"

// 解析 csv文件内容
void test_csv(void) {
	sccsv_t csv;
	int i, j;
	int rlen, clen;

	sl_start();

	// 这里得到 csv 对象
	csv = sccsv_create(_STR_PATH);
	if (NULL == csv)
		CERR_EXIT("open " _STR_PATH " is error!");

	//这里打印数据
	rlen = csv->rlen;
	clen = csv->clen;
	for (i = 0; i < rlen; ++i) {
		for (j = 0; j < clen; ++j) {
			printf("<%d, %d> => [%s]\n", i, j, sccsv_get(csv, i, j));
		}
	}

	//开心 测试圆满成功
	sccsv_delete(csv);
}
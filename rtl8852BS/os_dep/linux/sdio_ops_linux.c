/******************************************************************************
 *
 * Copyright(c) 2007 - 2021 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#define _SDIO_OPS_LINUX_C_

#include <drv_types.h>

static bool rtw_sdio_claim_host_needed(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	struct sdio_data *sdio_data = dvobj_to_sdio(dvobj);

	if (sdio_data->sys_sdio_irq_thd && sdio_data->sys_sdio_irq_thd == current)
		return _FALSE;
	return _TRUE;
}

#ifdef CONFIG_RTW_SDIO_RECORDS

#define DBG_SDIO_RECORD_DATA_LEN 4 /* record 4-byte at most by default */

#ifndef DBG_SDIO_RECORD_TASK_INFO
#define DBG_SDIO_RECORD_TASK_INFO 1
#endif

#ifndef DBG_SDIO_RECORDS_NUM
#define DBG_SDIO_RECORDS_NUM CONFIG_RTW_SDIO_RECORDS_NUM
#endif
#ifndef DBG_SDIO_RECORDS_ENABLE
#define DBG_SDIO_RECORDS_ENABLE CONFIG_RTW_SDIO_RECORDS_ENABLE
#endif
#ifndef DBG_SDIO_RECORDS_LOOP
#define DBG_SDIO_RECORDS_LOOP CONFIG_RTW_SDIO_RECORDS_LOOP
#endif

enum sdio_type {
	SDIO_CMDT_F0_52_READ,
	SDIO_CMDT_F0_52_WRITE,
	SDIO_CMDT_52_READ,
	SDIO_CMDT_52_WRITE,
	SDIO_CMDT_53_READ,
	SDIO_CMDT_53_WRITE,
	SDIO_CMDT_NUM,
};

static const char *sdio_type_str[] = {
	[SDIO_CMDT_F0_52_READ]	= "F052R",
	[SDIO_CMDT_F0_52_WRITE]	= "F052W",
	[SDIO_CMDT_52_READ]	= "52R",
	[SDIO_CMDT_52_WRITE]	= "52W",
	[SDIO_CMDT_53_READ]	= "53R",
	[SDIO_CMDT_53_WRITE]	= "53W",
};

struct sdio_record {
	bool valid;
	sysptime stime;
	sysptime etime;
	u8 type; /* enum sdio_type */
	u32 addr;
	u32 cnt;
	int err;
#if DBG_SDIO_RECORD_DATA_LEN
	u8 data[DBG_SDIO_RECORD_DATA_LEN];
#endif
#if DBG_SDIO_RECORD_TASK_INFO
	char task_comm[TASK_COMM_LEN];
	unsigned int cpu;
#endif
};

struct sdio_records {
#if CONFIG_RTW_SDIO_RECORDS_STATIC
	struct sdio_record record[DBG_SDIO_RECORDS_NUM];
#else
	struct sdio_record *record;
#endif

	size_t record_num;
	size_t pos;
	bool enable;
	bool loop;
};

static struct sdio_records dbg_sdio_records = {
#if CONFIG_RTW_SDIO_RECORDS_STATIC
	.record_num = DBG_SDIO_RECORDS_NUM,
	.enable = DBG_SDIO_RECORDS_ENABLE,
	.loop = DBG_SDIO_RECORDS_LOOP,
#endif
};

#define __dbg_sdio_record_warn(_r) do {} while (0)

#if DBG_SDIO_RECORD_DATA_LEN
#define __dbg_sdio_record_fill_data_1(_r, _data) (_r)->data[0] = *((u8 *)_data)
#define __dbg_sdio_record_fill_data_n(_r, _cnt, _data) _rtw_memcpy((_r)->data, _data, rtw_min(_cnt, DBG_SDIO_RECORD_DATA_LEN))
#define __dbg_sdio_record_fill_data_w(_r, _w) *((u16 *)(_r)->data) = cpu_to_le16(_w)
#define __dbg_sdio_record_fill_data_dw(_r, _dw) *((u32 *)(_r)->data) = cpu_to_le32(_dw)
#else
#define __dbg_sdio_record_fill_data_1(_r, _data) do {} while (0)
#define __dbg_sdio_record_fill_data_n(_r, _cnt, _data) do {} while (0)
#define __dbg_sdio_record_fill_data_w(_r, _w) do {} while (0)
#define __dbg_sdio_record_fill_data_dw(_r, _l) do {} while (0)
#endif

#if DBG_SDIO_RECORD_TASK_INFO
#define __dbg_sdio_record_fill_task_info(_r) \
	do { \
		_rtw_memcpy((_r)->task_comm, current->comm, TASK_COMM_LEN); \
		(_r)->cpu = smp_processor_id(); \
	} while (0)
#else
#define __dbg_sdio_record_fill_task_info(_r) do {} while (0)
#endif

#define DECLARE_DBG_SDIO_RECORD_P(_r) struct sdio_record *_r

#define dbg_sdio_record_get_and_advance(_r) \
	do { \
		if (dbg_sdio_records.enable) { \
			if (dbg_sdio_records.loop || !dbg_sdio_records.record[dbg_sdio_records.pos].valid) { \
				(_r) = &dbg_sdio_records.record[dbg_sdio_records.pos]; \
				dbg_sdio_records.pos++; \
				if (dbg_sdio_records.pos >= dbg_sdio_records.record_num) \
					dbg_sdio_records.pos = 0; \
				(_r)->valid = true; \
				__dbg_sdio_record_fill_task_info(_r); \
				(_r)->stime = rtw_sptime_get_raw(); \
			} else { \
				RTW_INFO("%s record full, disable\n", __func__); \
				dbg_sdio_records.enable = false; \
			} \
		} \
	} while (0)

#define dbg_sdio_record_fill_1(_r, _type, _addr, _err, _data) \
	do { \
		if (dbg_sdio_records.enable) { \
			(_r)->etime = rtw_sptime_get_raw(); \
			(_r)->type = _type; \
			(_r)->addr = _addr; \
			(_r)->cnt = 1; \
			(_r)->err = _err; \
			__dbg_sdio_record_fill_data_1(_r, _data); \
			__dbg_sdio_record_warn(_r); \
		} \
	} while (0)

#define dbg_sdio_record_fill_n(_r, _type, _addr, _cnt, _err, _data) \
	do { \
		if (dbg_sdio_records.enable) { \
			(_r)->etime = rtw_sptime_get_raw(); \
			(_r)->type = _type; \
			(_r)->addr = _addr; \
			(_r)->cnt = _cnt; \
			(_r)->err = _err; \
			__dbg_sdio_record_fill_data_n(_r, _cnt, _data); \
			__dbg_sdio_record_warn(_r); \
		} \
	} while (0)

#define dbg_sdio_record_fill_w(_r, _type, _addr, _err, _w) \
	do { \
		if (dbg_sdio_records.enable) { \
			(_r)->etime = rtw_sptime_get_raw(); \
			(_r)->type = _type; \
			(_r)->addr = _addr; \
			(_r)->cnt = 2; \
			(_r)->err = _err; \
			__dbg_sdio_record_fill_data_w(_r, _w); \
			__dbg_sdio_record_warn(_r); \
		} \
	} while (0)

#define dbg_sdio_record_fill_dw(_r, _type, _addr, _err, _dw) \
	do { \
		if (dbg_sdio_records.enable) { \
			(_r)->etime = rtw_sptime_get_raw(); \
			(_r)->type = _type; \
			(_r)->addr = _addr; \
			(_r)->cnt = 4; \
			(_r)->err = _err; \
			__dbg_sdio_record_fill_data_dw(_r, _dw); \
			__dbg_sdio_record_warn(_r); \
		} \
	} while (0)

#define SDIO_R_TITLE_FMT "%-9s %-17s %-11s %-5s %-7s %-5s %-4s"
#define SDIO_R_TITLE_TFMT "%s\t%s\t%s\t%s\t%s\t%s\t%s"
#define SDIO_R_TITLE_ARG , "seq", "stime", "api_time", "type", "addr", "cnt", "err"
#define SDIO_R_VALUE_FMT "%9zu %7lld.%09lld %lld.%09lld %-5s 0x%05x %5u %4d"
#define SDIO_R_VALUE_TFMT "%zu\t%lld.%09lld\t%lld.%09lld\t%s\t0x%05x\t%u\t%d"
#define SDIO_R_VALUE_ARG \
		, seq \
		, rtw_sptime_to_ns(r->stime) / 1000000000, rtw_sptime_to_ns(r->stime) % 1000000000 \
		, rtw_sptime_diff_ns(r->stime, r->etime) / 1000000000, rtw_sptime_diff_ns(r->stime, r->etime) % 1000000000 \
		, sdio_type_str[r->type] \
		, r->addr, r->cnt, r->err

#if DBG_SDIO_RECORD_DATA_LEN
#define SDIO_R_TITLE_FMT_DATA " %-2s %-2s %-2s %-2s"
#define SDIO_R_TITLE_TFMT_DATA "\t%s\t%s\t%s\t%s"
#define SDIO_R_TITLE_ARG_DATA , "d0", "d1", "d2", "d3"
#define SDIO_R_VALUE_FMT_DATA " %02x %02x %02x %02x"
#define SDIO_R_VALUE_TFMT_DATA "\t%02x\t%02x\t%02x\t%02x"
#define SDIO_R_VALUE_ARG_DATA  , r->cnt > 0 ? r->data[0] : 0, r->cnt > 1 ? r->data[1] : 0, r->cnt > 2 ? r->data[2] : 0, r->cnt > 3 ? r->data[3] : 0
#else
#define SDIO_R_TITLE_FMT_DATA ""
#define SDIO_R_TITLE_TFMT_DATA ""
#define SDIO_R_TITLE_ARG_DATA
#define SDIO_R_VALUE_FMT_DATA ""
#define SDIO_R_VALUE_TFMT_DATA ""
#define SDIO_R_VALUE_ARG_DATA
#endif

#if DBG_SDIO_RECORD_TASK_INFO
#define SDIO_R_TITLE_FMT_TASK_INFO " %-15s %-3s"
#define SDIO_R_TITLE_TFMT_TASK_INFO "\t%s\t%s"
#define SDIO_R_TITLE_ARG_TASK_INFO , "task_comm", "cpu"
#define SDIO_R_VALUE_FMT_TASK_INFO " %-15s %-3d"
#define SDIO_R_VALUE_TFMT_TASK_INFO "\t%s\t%d"
#define SDIO_R_VALUE_ARG_TASK_INFO , r->task_comm, r->cpu
#else
#define SDIO_R_TITLE_FMT_TASK_INFO ""
#define SDIO_R_TITLE_TFMT_TASK_INFO ""
#define SDIO_R_TITLE_ARG_TASK_INFO
#define SDIO_R_VALUE_FMT_TASK_INFO ""
#define SDIO_R_VALUE_TFMT_TASK_INFO ""
#define SDIO_R_VALUE_ARG_TASK_INFO
#endif

#define SDIO_R_TITLE_FMT_REL " %-11s %-11s"
#define SDIO_R_TITLE_TFMT_REL "\t%s\t%s"
#define SDIO_R_TITLE_ARG_REL , "from_last_io", "from_last_rx_io"
#define SDIO_R_VALUE_FMT_REL " %2lld.%09lld %5lld.%09lld"
#define SDIO_R_VALUE_TFMT_REL "\t%lld.%09lld\t%lld.%09lld"
#define SDIO_R_VALUE_ARG_REL \
		, last_io ? rtw_sptime_diff_ns(last_io->etime, r->stime) / 1000000000 : 0 \
		, last_io ? rtw_sptime_diff_ns(last_io->etime, r->stime) % 1000000000 : 0 \
		, last_rx_io ? rtw_sptime_diff_ns(last_rx_io->etime, r->stime) / 1000000000 : 0 \
		, last_rx_io ? rtw_sptime_diff_ns(last_rx_io->etime, r->stime) % 1000000000 : 0

static void sdio_record_title_dump_tab(void *sel)
{
	RTW_PRINT_SEL(sel, SDIO_R_TITLE_TFMT
		SDIO_R_TITLE_TFMT_DATA
		SDIO_R_TITLE_TFMT_TASK_INFO
		SDIO_R_TITLE_TFMT_REL
		"\n"
		SDIO_R_TITLE_ARG
		SDIO_R_TITLE_ARG_DATA
		SDIO_R_TITLE_ARG_TASK_INFO
		SDIO_R_TITLE_ARG_REL
		);
}

static void sdio_record_value_dump_tab(void *sel, struct sdio_record *r, size_t seq
	, struct sdio_record *last_io
	, struct sdio_record *last_rx_io)
{
	RTW_PRINT_SEL(sel, SDIO_R_VALUE_TFMT
		SDIO_R_VALUE_TFMT_DATA
		SDIO_R_VALUE_TFMT_TASK_INFO
		SDIO_R_VALUE_TFMT_REL
		"\n"
		SDIO_R_VALUE_ARG
		SDIO_R_VALUE_ARG_DATA
		SDIO_R_VALUE_ARG_TASK_INFO
		SDIO_R_VALUE_ARG_REL
	);
}


static void sdio_record_title_dump(void *sel)
{
	RTW_PRINT_SEL(sel, SDIO_R_TITLE_FMT
		SDIO_R_TITLE_FMT_DATA
		SDIO_R_TITLE_FMT_TASK_INFO
		SDIO_R_TITLE_FMT_REL
		"\n"
		SDIO_R_TITLE_ARG
		SDIO_R_TITLE_ARG_DATA
		SDIO_R_TITLE_ARG_TASK_INFO
		SDIO_R_TITLE_ARG_REL
		);
}

static void sdio_record_value_dump(void *sel, struct sdio_record *r, size_t seq
	, struct sdio_record *last_io
	, struct sdio_record *last_rx_io)
{
	RTW_PRINT_SEL(sel, SDIO_R_VALUE_FMT
		SDIO_R_VALUE_FMT_DATA
		SDIO_R_VALUE_FMT_TASK_INFO
		SDIO_R_VALUE_FMT_REL
		"\n"
		SDIO_R_VALUE_ARG
		SDIO_R_VALUE_ARG_DATA
		SDIO_R_VALUE_ARG_TASK_INFO
		SDIO_R_VALUE_ARG_REL
	);
}

typedef void (*sdio_rec_title_dump)(void *);
typedef void (*sdio_rec_value_dump)(void *, struct sdio_record *, size_t, struct sdio_record *, struct sdio_record *);

static bool rtw_sdio_record_is_rx(struct sdio_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01f00;
}

bool rtw_sdio_records_enabled(void)
{
	return dbg_sdio_records.enable;
}

void rtw_sdio_records_clear(void)
{
	int i;

	for (i = 0; i < dbg_sdio_records.record_num; i++)
		dbg_sdio_records.record[i].valid = false;
	dbg_sdio_records.pos = 0;
}

bool rtw_sdio_record_valid(size_t seq)
{
	if (seq < dbg_sdio_records.record_num) {
		size_t oldest_pos = dbg_sdio_records.record[dbg_sdio_records.pos].valid ? dbg_sdio_records.pos : 0;
		struct sdio_record *record = &dbg_sdio_records.record[(oldest_pos + seq) % dbg_sdio_records.record_num];

		return record->valid;
	}
	return false;
}

void rtw_sdio_records_dump_title(void *sel, bool tab)
{
	sdio_rec_title_dump title_dump = tab ? sdio_record_title_dump_tab : sdio_record_title_dump;

	title_dump(sel);
}

void rtw_sdio_records_dump_value_by_seq(void *sel, bool tab, size_t seq)
{
	size_t oldest_pos = dbg_sdio_records.record[dbg_sdio_records.pos].valid ? dbg_sdio_records.pos : 0;
	struct sdio_record *record = &dbg_sdio_records.record[(oldest_pos + seq) % dbg_sdio_records.record_num];

	if (record->valid) {
		sdio_rec_value_dump value_dump = tab ? sdio_record_value_dump_tab : sdio_record_value_dump;
		bool rx_io = rtw_sdio_record_is_rx(record);
		struct sdio_record *last_io = NULL;
		struct sdio_record *last_rx_io = NULL;

		if (seq != 0) {
			last_io = &dbg_sdio_records.record[(oldest_pos + seq - 1) % dbg_sdio_records.record_num];

			if (rx_io) {
				struct sdio_record *r;
				size_t i;

				/* find last_rx_io */
				for (i = 1; i <= seq ; i++) {
					r = &dbg_sdio_records.record[(oldest_pos + seq - i) % dbg_sdio_records.record_num];
					if (rtw_sdio_record_is_rx(r)) {
						last_rx_io = r;
						break;
					}
				}
			}
		}
		value_dump(sel, record, seq, last_io, rx_io ? last_rx_io : NULL);
	}
}

void rtw_sdio_records_dump(void *sel, bool tab)
{
	struct sdio_record *record;
	struct sdio_record *last_io = NULL;
	struct sdio_record *last_rx_io = NULL;
	sdio_rec_title_dump title_dump = tab ? sdio_record_title_dump_tab : sdio_record_title_dump;
	sdio_rec_value_dump value_dump = tab ? sdio_record_value_dump_tab : sdio_record_value_dump;
	bool rx_io;
	size_t oldest_pos = dbg_sdio_records.record[dbg_sdio_records.pos].valid ? dbg_sdio_records.pos : 0;
	size_t i;

	title_dump(sel);

	for (i = 0; i < dbg_sdio_records.record_num; i++) {
		record = &dbg_sdio_records.record[(oldest_pos + i) % dbg_sdio_records.record_num];
		if (!record->valid)
			break;
		rx_io = rtw_sdio_record_is_rx(record);
		value_dump(sel, record, i, last_io, rx_io ? last_rx_io : NULL);
		last_io = record;
		if (rx_io)
			last_rx_io = record;
	}
}

void rtw_sdio_records_claim_and_enable(struct dvobj_priv *d, bool enable)
{
	struct sdio_func *func;
	bool claim_needed;

#if !CONFIG_RTW_SDIO_RECORDS_STATIC
	if (!dbg_sdio_records.record) {
		rtw_warn_on(1);
		return;
	}
#endif

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	if (claim_needed)
		sdio_claim_host(func);

	RTW_INFO("%s enable:%d\n", __func__, enable);
	dbg_sdio_records.enable = enable;

	if (claim_needed)
		sdio_release_host(func);
}

void rtw_sdio_records_claim_and_dump(void *sel, struct dvobj_priv *d, bool tab)
{
	struct sdio_func *func = dvobj_to_sdio_func(d);
	bool claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);

	rtw_sdio_records_dump(sel, tab);

	if (claim_needed)
		sdio_release_host(func);
}

int rtw_sdio_records_init(void)
{
#if !CONFIG_RTW_SDIO_RECORDS_STATIC
extern uint rtw_sdio_records_num;
extern uint rtw_sdio_records_enable;
extern uint rtw_sdio_records_loop;

	size_t record_num = rtw_sdio_records_num;
	bool enable = !!rtw_sdio_records_enable;
	bool loop = !!rtw_sdio_records_loop;

	dbg_sdio_records.record = rtw_zvmalloc(sizeof(struct sdio_record) * record_num);
	if (!dbg_sdio_records.record)
		return _FAIL;

	dbg_sdio_records.record_num = record_num;
	dbg_sdio_records.loop = loop;
	dbg_sdio_records.pos = 0;
	dbg_sdio_records.enable = enable;
#endif
	return _SUCCESS;
}

void rtw_sdio_records_deinit(void)
{
#if !CONFIG_RTW_SDIO_RECORDS_STATIC
	if (dbg_sdio_records.record)
		rtw_vmfree(dbg_sdio_records.record, sizeof(struct sdio_record) * dbg_sdio_records.record_num);
#endif
}

#else
#define DECLARE_DBG_SDIO_RECORD_P(_r)
#define dbg_sdio_record_get_and_advance(_r) do {} while (0)
#define dbg_sdio_record_fill_1(_r, _type, _addr, _err, _data) do {} while (0)
#define dbg_sdio_record_fill_n(_r, _type, _addr, _cnt, _err, _data) do {} while (0)
#define dbg_sdio_record_fill_w(_r, _type, _addr, _err, _w) do {} while (0)
#define dbg_sdio_record_fill_dw(_r, _type, _addr, _err, _dw) do {} while (0)
#endif /* CONFIG_RTW_SDIO_RECORDS */

/*#define RTW_SDIO_DUMP*/
#ifdef RTW_SDIO_DUMP
#define DUMP_LEN_LMT	0	/* buffer dump size limit */
				/* unit: byte, 0 for no limit */
#else
#define DUMP_LEN_LMT	32
#endif
#define GET_DUMP_LEN(len)	(DUMP_LEN_LMT ? rtw_min(len, DUMP_LEN_LMT) : len)

#ifdef DBG_SDIO
#if (DBG_SDIO >= 1)
static void sdio_dump_reg_by_cmd52(struct dvobj_priv *d,
				   u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	u8 read_twice = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);
	/*
	 * When register is WLAN IOREG,
	 * read twice to guarantee the result is correct.
	 */
	if (addr & 0x10000)
		read_twice = 1;

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_readb(func, addr, &error);
		if (read_twice)
			val = sdio_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ 0x%02x FAIL!", addr);
}

static void sdio_dump_cia(struct dvobj_priv *d, u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_f0_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ CIA 0x%02x FAIL!",
			addr);
}

#if (DBG_SDIO >= 2)
void rtw_sdio_dbg_reg_alloc(struct dvobj_priv *d);
#endif /* DBG_SDIO >= 2 */

/*
 * Dump register when CMD53 fail
 */
static void sdio_dump_dbg_reg(struct dvobj_priv *d, u8 write,
			      unsigned int addr, size_t len)
{
	struct sdio_data *sdio;
	struct sdio_func *func;
	u8 *buf = NULL;
#if (DBG_SDIO >= 2)
	u8 *msg;
#endif /* DBG_SDIO >= 2 */


	sdio = dvobj_to_sdio(d);
	if (sdio->reg_dump_mark)
		return;
	func = dvobj_to_sdio_func(d);

	sdio->reg_dump_mark = sdio->cmd53_err_cnt;

#if (DBG_SDIO >= 2)
	if (!sdio->dbg_msg) {
		msg = rtw_zmalloc(80);
		if (msg) {
			sdio->dbg_msg = msg;
			sdio->dbg_msg_size = 80;
		}
	}
	if (sdio->dbg_msg_size) {
		snprintf(sdio->dbg_msg, sdio->dbg_msg_size,
			 "CMD53 %s 0x%05x, %zu bytes FAIL "
			 "at err_cnt=%d",
			 write?"WRITE":"READ",
			 addr, len, sdio->reg_dump_mark);
	}

	rtw_sdio_dbg_reg_alloc(d);
#endif /* DBG_SDIO >= 2 */

	/* MAC register */
	dev_err(&func->dev, "MAC register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x10000, 0x800, buf);
	dev_err(&func->dev, "MAC Extend register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac_ext;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x11000, 0x800, buf);

	/* SDIO local register */
	dev_err(&func->dev, "SDIO Local register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_local;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x0, 0x100, buf);

	/* F0 */
	dev_err(&func->dev, "f0 register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_cia;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_cia(d, 0x0, 0x200, buf);
}
#endif /* DBG_SDIO >= 1 */
#endif /* DBG_SDIO */

/**
 *	Returns driver error code,
 *	0	no error
 *	-1	Level 1 error, critical error and can't be recovered
 *	-2	Level 2 error, normal error, retry to recover is possible
 */
static int linux_io_err_to_drv_err(int err)
{
	if (!err)
		return 0;

	/* critical error */
	if ((err == -ESHUTDOWN) ||
	    (err == -ENODEV) ||
	    (err == -ENOMEDIUM))
		return -1;

	/* other error */
	return -2;
}

/**
 *	rtw_sdio_raw_read - Read from SDIO device
 *	@d: driver object private data
 *	@addr: address to read
 *	@buf: buffer to store the data
 *	@len: number of bytes to read
 *	@fixed:
 *
 *	Reads from the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_read(struct dvobj_priv *d, unsigned int addr,
				   void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;
	struct sdio_data *sdio;
	u8 *tmpbuf = NULL;
	DECLARE_DBG_SDIO_RECORD_P(r);

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);
	sdio = dvobj_to_sdio(d);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "RTW_SDIO: READ F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "RTW_SDIO: READ use CMD52\n");
	else
		dev_dbg(&func->dev, "RTW_SDIO: READ use CMD53\n");

	dev_dbg(&func->dev, "RTW_SDIO: READ from 0x%05x\n", addr);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			dbg_sdio_record_get_and_advance(r);
			((u8 *)buf)[i] = sdio_f0_readb(func, offset, &error);
			dbg_sdio_record_fill_1(r, SDIO_CMDT_F0_52_READ, offset, error, ((u8 *)buf) + i);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 read 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				dbg_sdio_record_get_and_advance(r);
				((u8 *)buf)[i] = sdio_readb(func, offset, &error);
				dbg_sdio_record_fill_1(r, SDIO_CMDT_52_READ, offset, error, ((u8 *)buf) + i);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (len <= sdio->tmpbuf_sz) {
				tmpbuf = buf;
				buf = sdio->tmpbuf;
			}
			if (fixed) {
				dbg_sdio_record_get_and_advance(r);
				error = sdio_readsb(func, buf, addr, len);
				dbg_sdio_record_fill_n(r, SDIO_CMDT_53_READ, addr, len, error, buf);
			} else {
				dbg_sdio_record_get_and_advance(r);
				error = sdio_memcpy_fromio(func, buf, addr, len);
				dbg_sdio_record_fill_n(r, SDIO_CMDT_53_READ, addr, len, error, buf);
			}
			if (!error && tmpbuf)
				_rtw_memcpy(tmpbuf, buf, len);
		}
	}

#ifdef DBG_SDIO
#if (DBG_SDIO >= 3)
	if (!error && !f0 && !cmd52
	    && (sdio->dbg_enable
		&& sdio->err_test && !sdio->err_test_triggered
		&& ((addr & 0x10000)
		    || (!(addr & 0xE000)
			&& !((addr >= 0x40) && (addr < 0x48)))))) {
		sdio->err_test_triggered = 1;
		error = -ETIMEDOUT;
		dev_warn(&func->dev, "Simulate error(%d) READ addr=0x%05x %zu bytes",
			 error, addr, len);
	}
#endif /* DBG_SDIO >= 3 */

	if (error) {
		if (f0 || cmd52) {
			sdio->cmd52_err_cnt++;
		} else {
			sdio->cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 0, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

#ifdef RTW_SDIO_DUMP
	print_hex_dump(KERN_DEBUG, "RTW_SDIO: READ ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio read failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "RTW_SDIO: READ F0\n");
		if (cmd52)
			dev_err(&func->dev, "RTW_SDIO: READ use CMD52\n");
		else
			dev_err(&func->dev, "RTW_SDIO: READ use CMD53\n");
		dev_err(&func->dev, "RTW_SDIO: READ from 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "RTW_SDIO: READ ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}

/**
 *	rtw_sdio_raw_write - Write to SDIO device
 *	@d: driver object private data
 *	@addr: address to write
 *	@buf: buffer that contains the data to write
 *	@len: number of bytes to write
 *	@fixed: address is fixed(FIFO) or incremented
 *
 *	Writes to the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_write(struct dvobj_priv *d, unsigned int addr,
				    void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;
	struct sdio_data *sdio;
	DECLARE_DBG_SDIO_RECORD_P(r);

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);
	sdio = dvobj_to_sdio(d);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "RTW_SDIO: WRITE F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "RTW_SDIO: WRITE use CMD52\n");
	else
		dev_dbg(&func->dev, "RTW_SDIO: WRITE use CMD53\n");
	dev_dbg(&func->dev, "RTW_SDIO: WRITE to 0x%05x\n", addr);
	print_hex_dump(KERN_DEBUG, "RTW_SDIO: WRITE ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			dbg_sdio_record_get_and_advance(r);
			sdio_f0_writeb(func, ((u8 *)buf)[i], offset, &error);
			dbg_sdio_record_fill_1(r, SDIO_CMDT_F0_52_WRITE, offset, error, ((u8 *)buf) + i);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 write 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				dbg_sdio_record_get_and_advance(r);
				sdio_writeb(func, ((u8 *)buf)[i], offset, &error);
				dbg_sdio_record_fill_1(r, SDIO_CMDT_52_WRITE, offset, error, ((u8 *)buf) + i);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (len <= sdio->tmpbuf_sz) {
				_rtw_memcpy(sdio->tmpbuf, buf, len);
				buf = sdio->tmpbuf;
			}
			if (fixed) {
				dbg_sdio_record_get_and_advance(r);
				error = sdio_writesb(func, addr, buf, len);
				dbg_sdio_record_fill_n(r, SDIO_CMDT_53_WRITE, addr, len, error, buf);
			} else {
				dbg_sdio_record_get_and_advance(r);
				error = sdio_memcpy_toio(func, addr, buf, len);
				dbg_sdio_record_fill_n(r, SDIO_CMDT_53_WRITE, addr, len, error, buf);
			}
		}
	}

#ifdef DBG_SDIO
	if (error) {
		if (f0 || cmd52) {
			sdio->cmd52_err_cnt++;
		} else {
			sdio->cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 1, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio write failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "RTW_SDIO: WRITE F0\n");
		if (cmd52)
			dev_err(&func->dev, "RTW_SDIO: WRITE use CMD52\n");
		else
			dev_err(&func->dev, "RTW_SDIO: WRITE use CMD53\n");
		dev_err(&func->dev, "RTW_SDIO: WRITE to 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "RTW_SDIO: WRITE ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}

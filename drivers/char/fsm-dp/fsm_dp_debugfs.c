/* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "fsm_dp.h"
#ifdef CONFIG_DEBUG_FS

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#define MEM_DUMP_COL_WIDTH 16
#define MAX_MEM_DUMP_SIZE 256
#define MILISEC 1000 /* milisecond in terms of microsecond */
#define TWO_MILISEC (2 * MILISEC)

#define DEFINE_DEBUGFS_OPS(name, __read, __write)		\
static int name ##_open(struct inode *inode, struct file *file)	\
{								\
	return single_open(file, __read, inode->i_private);	\
}								\
static const struct file_operations name ##_ops = {		\
	.open	 = name ## _open,				\
	.read = seq_read,					\
	.write = __write,					\
	.llseek = seq_lseek,					\
	.release = single_release,				\
}

static struct dentry *__dent;

static int __fsm_dp_rxqueue_vma_dump(
	struct seq_file *s,
	struct fsm_dp_rxqueue_vma *rxq_vma)
{
	if (rxq_vma->vma) {
		struct vm_area_struct *vma = rxq_vma->vma;

		seq_printf(s, "    Type:               %s\n",
			   fsm_dp_rx_type_to_str(rxq_vma->type));
		seq_printf(s, "    RefCnt:             %d\n",
			   atomic_read(&rxq_vma->refcnt));
		seq_printf(s,
			   "        vm_start:       %lx\n"
			   "        vm_end:         %lx\n"
			   "        vm_pgoff:       %lx\n"
			   "        vm_flags:       %lx\n",
			   vma->vm_start,
			   vma->vm_end,
			   vma->vm_pgoff,
			   vma->vm_flags);
	}
	return 0;
}

static int __fsm_dp_mempool_vma_dump(
	struct seq_file *s,
	struct fsm_dp_mempool_vma *mempool_vma)
{
	struct fsm_dp_mempool *mempool = *mempool_vma->pp_mempool;
	struct vm_area_struct *vma;
	int i;

	if (mempool)
		seq_printf(s, "    Type:               %s\n",
			   fsm_dp_mem_type_to_str(mempool->type));

	for (i = 0; i < FSM_DP_MMAP_TYPE_LAST; i++) {
		if (mempool_vma->vma[i]) {
			vma = mempool_vma->vma[i];
			seq_printf(s, "    VMA[%d]:             %s\n",
				   i, fsm_dp_mmap_type_to_str(i));
			seq_printf(s,
				   "        vm_start:       %lx\n"
				   "        vm_end:         %lx\n"
				   "        vm_pgoff:       %lx\n"
				   "        vm_flags:       %lx\n",
				   vma->vm_start,
				   vma->vm_end,
				   vma->vm_pgoff,
				   vma->vm_flags);
			seq_printf(s, "        refcnt:         %d\n",
				   atomic_read(&mempool_vma->refcnt[i]));
		}
	}
	return 0;
}

static int __fsm_dp_ring_opstats_dump(
	struct seq_file *s,
	struct fsm_dp_ring_opstats *stats)
{
	seq_puts(s, "Read:\n");
	seq_printf(s, "    Ok:                %lu\n", stats->read_ok);
	seq_printf(s, "    Empty:             %lu\n", stats->read_empty);
	seq_printf(s, "    TailUpdt:          %lu\n", stats->cons_tail_updt);
	seq_printf(s, "    TailNoUpdt:        %lu\n", stats->cons_tail_no_updt);
	seq_printf(s, "    TailUpdtBackOff:   %lu\n",
		   stats->cons_tail_updt_backoff);
	seq_printf(s, "    TailUpdtStop:      %lu\n",
		   stats->cons_tail_updt_stop);
	seq_printf(s, "    HdrUpdtRetry:      %lu\n",
		   stats->cons_head_updt_retry);
	seq_puts(s, "Write:\n");
	seq_printf(s, "    Ok:                %lu\n", stats->write_ok);
	seq_printf(s, "    Full:              %lu\n", stats->write_full);
	seq_printf(s, "    TailUpdt:          %lu\n", stats->prod_tail_updt);
	seq_printf(s, "    TailNoUpdt:        %lu\n", stats->prod_tail_no_updt);
	seq_printf(s, "    TailUpdtBackOff:   %lu\n",
		   stats->prod_tail_updt_backoff);
	seq_printf(s, "    TailUpdtStop:      %lu\n",
		   stats->prod_tail_updt_stop);
	seq_printf(s, "    HdrUpdtRetry:      %lu\n",
		   stats->prod_head_updt_retry);
	return 0;
}

static int __fsm_dp_ring_runtime_dump(
	struct seq_file *s,
	struct fsm_dp_ring *ring)
{
	seq_printf(s, "ProdHdr:                %u\n", *ring->prod_head);
	seq_printf(s, "ProdTail:               %u\n", *ring->prod_tail);
	seq_printf(s, "ConsHdr:                %u\n", *ring->cons_head);
	seq_printf(s, "ConsTail:               %u\n", *ring->cons_tail);
	seq_printf(s, "NumOfElementAvail:      %u\n",
		   (*ring->prod_head - *ring->cons_tail) & (ring->size - 1));
	return 0;
}

static int __fsm_dp_ring_config_dump(
	struct seq_file *s,
	struct fsm_dp_ring *ring)
{
	seq_printf(s, "Ring %llx MemoryAlloc:\n", (u64) ring);
	seq_printf(s, "         AllocAddr:     %llx\n", (u64) ring->loc.base);
	seq_printf(s, "         AllocSize:     0x%08lx\n", ring->loc.size);
	seq_printf(s, "         MmapCookie:    0x%08x\n", ring->loc.cookie);
	seq_printf(s, "Size:                   0x%x\n", ring->size);
	seq_printf(s, "ProdHdr:                %llx\n", (u64) ring->prod_head);
	seq_printf(s, "ProdTail:               %llx\n", (u64) ring->prod_tail);
	seq_printf(s, "ConsHdr:                %llx\n", (u64) ring->cons_head);
	seq_printf(s, "ConsTail:               %llx\n", (u64) ring->cons_tail);
	seq_printf(s, "RingBuf:                %llx\n", (u64) ring->element);
	return 0;
}

static int debugfs_loopback_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_loopback_task *task =
		(struct fsm_dp_loopback_task *)s->private;

	seq_puts(s, "TX Loopback\n");
	seq_printf(s, "    Count:              %lu\n", task->stats.tx_cnt);
	seq_printf(s, "    Enqueue:            %lu\n", task->stats.tx_enque);
	seq_printf(s, "    Error:              %lu\n", task->stats.tx_err);
	seq_printf(s, "    Drop:               %lu\n", task->stats.tx_drop);
	seq_puts(s, "RX Loopback\n");
	seq_printf(s, "    Count:              %lu\n", task->stats.rx_cnt);
	seq_printf(s, "    Enqueue:            %lu\n", task->stats.rx_enque);
	seq_printf(s, "    Error:              %lu\n", task->stats.rx_err);
	seq_printf(s, "    Drop:               %lu\n", task->stats.rx_drop);
	seq_printf(s, "Run:                    %lu\n", task->stats.run);
	seq_printf(s, "Schedule:               %lu\n", task->stats.sched);

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_loopback, debugfs_loopback_read, NULL);

static int debugfs_rxq_refcnt_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_rxqueue *rxq = (struct fsm_dp_rxqueue *)s->private;

	if (rxq->inited)
		seq_printf(s, "%d\n", atomic_read(&rxq->refcnt));

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_rxq_refcnt, debugfs_rxq_refcnt_read, NULL);

static int debugfs_rxq_opstats_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_rxqueue *rxq = (struct fsm_dp_rxqueue *)s->private;

	if (rxq->inited)
		__fsm_dp_ring_opstats_dump(s, &rxq->ring.opstats);

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_rxq_opstats, debugfs_rxq_opstats_read, NULL);

static int debugfs_rxq_config_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_rxqueue *rxq = (struct fsm_dp_rxqueue *)s->private;

	if (rxq->inited) {
		seq_printf(s, "Type:                   %s\n",
			   fsm_dp_rx_type_to_str(rxq->type));
		__fsm_dp_ring_config_dump(s, &rxq->ring);
	}

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_rxq_config, debugfs_rxq_config_read, NULL);

static int debugfs_rxq_runtime_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_rxqueue *rxq = (struct fsm_dp_rxqueue *)s->private;

	if (rxq->inited)
		__fsm_dp_ring_runtime_dump(s, &rxq->ring);

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_rxq_runtime, debugfs_rxq_runtime_read, NULL);

static unsigned int __mem_dump_size[FSM_DP_MEM_TYPE_LAST];
static unsigned int __mem_offset[FSM_DP_MEM_TYPE_LAST];

static int debugfs_mem_data_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool) {
		struct fsm_dp_mem *mem = &mempool->mem;
		unsigned int n = __mem_dump_size[mempool->type];
		unsigned int offset = __mem_offset[mempool->type];
		unsigned int i, j;
		unsigned int cluster, c_offset;
		unsigned char *data = (unsigned char *)mem->loc.base + offset;

		data = fsm_dp_mem_offset_addr(mem, offset, &cluster, &c_offset);
		if (data == NULL)
			return 0;
		if (n > (mem->loc.size - offset))
			n = mem->loc.size - offset;

		for (i = 0; i < offset % MEM_DUMP_COL_WIDTH; i++)
			seq_puts(s, "   ");

		for (j = 0; j < n; j++, i++) {
			if (i && !(i % MEM_DUMP_COL_WIDTH))
				seq_puts(s, "\n");
			seq_printf(s, "%02x ", *data);
			data++;
			c_offset++;
			if (c_offset >= FSM_DP_MEMPOOL_CLUSTER_SIZE) {
				c_offset = 0;
				cluster++;
				data = mem->loc.cluster_kernel_addr[cluster];
			}
		}
		seq_puts(s, "\n");
	}
	return 0;
}

static ssize_t debugfs_mem_data_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));

	if (mempool) {
		struct fsm_dp_mem *mem = &mempool->mem;
		unsigned int value = 0;
		unsigned int *data;
		unsigned int offset = __mem_offset[mempool->type];
		unsigned int cluster, c_offset;

		if (kstrtouint_from_user(buf, count, 0, &value))
			return -EFAULT;
		data = (unsigned int *)fsm_dp_mem_offset_addr(
				mem, offset, &cluster, &c_offset);
		if (data == NULL)
			return count;
		*data = value;
	}
	return count;
}
DEFINE_DEBUGFS_OPS(debugfs_mem_data, debugfs_mem_data_read,
		   debugfs_mem_data_write);

static int debugfs_mem_dump_size_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "%u\n", __mem_dump_size[mempool->type]);
	return 0;
}

static ssize_t debugfs_mem_dump_size_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));
	unsigned int value = 0;

	if (!mempool)
		goto done;

	if (kstrtouint_from_user(buf, count, 0, &value))
		return -EFAULT;

	if (value > MAX_MEM_DUMP_SIZE)
		return -EINVAL;

	__mem_dump_size[mempool->type] = value;
done:
	return count;
}
DEFINE_DEBUGFS_OPS(debugfs_mem_dump_size, debugfs_mem_dump_size_read,
		   debugfs_mem_dump_size_write);

static int debugfs_mem_offset_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "0x%08x\n", __mem_offset[mempool->type]);
	return 0;
}

static ssize_t debugfs_mem_offset_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));

	if (mempool) {
		struct fsm_dp_mem *mem = &mempool->mem;
		unsigned int value = 0;

		if (kstrtouint_from_user(buf, count, 0, &value))
			return -EFAULT;

		if (value >= mem->loc.size)
			return -EINVAL;
		if (value & 3)
			return -EINVAL;

		__mem_offset[mempool->type] = value;
	}
	return count;
}
DEFINE_DEBUGFS_OPS(debugfs_mem_offset, debugfs_mem_offset_read,
		   debugfs_mem_offset_write);

static int debugfs_mem_config_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);
	int i;

	if (mempool) {
		struct fsm_dp_mem *mem = &mempool->mem;

		seq_puts(s, "MemoryAlloc:\n");
		seq_printf(s, "    AllocSize:     0x%08lx\n",
			   mem->loc.size);
		seq_printf(s, "    Total Cluster:  %d\n",
			   mem->loc.num_cluster);
		seq_printf(s, "    Cluster Size:  0x%x\n",
			   FSM_DP_MEMPOOL_CLUSTER_SIZE);
		for (i = 0; i < mem->loc.num_cluster; i++)
			seq_printf(s, "    Cluster %d Addr: %llx\n", i,
					(u64) mem->loc.cluster_kernel_addr[i]);
		seq_printf(s, "    Buffer Per Cluster:  %d\n",
			   mem->loc.buf_per_cluster);
		seq_printf(s, "    Last Cluster Order:  %d\n",
			   mem->loc.last_cl_order);
		seq_printf(s, "    MmapCookie:    %08x\n",
			   mem->loc.cookie);
		seq_printf(s, "BufSize:                0x%x\n", mem->buf_sz);
		seq_printf(s, "BufCount:               0x%x\n", mem->buf_cnt);
		seq_printf(s, "BufTrueSize:            0x%x\n",
			   fsm_dp_buf_true_size(mem));

	}

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mem_config, debugfs_mem_config_show, NULL);

static int debugfs_ring_config_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		__fsm_dp_ring_config_dump(s, &mempool->ring);
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_ring_config, debugfs_ring_config_read, NULL);

static int debugfs_ring_runtime_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		__fsm_dp_ring_runtime_dump(s, &mempool->ring);
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_ring_runtime, debugfs_ring_runtime_read, NULL);

static int debugfs_ring_opstats_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		__fsm_dp_ring_opstats_dump(s, &mempool->ring.opstats);
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_ring_opstats, debugfs_ring_opstats_read, NULL);

unsigned long __ring_index[FSM_DP_MEM_TYPE_LAST];

static int debugfs_ring_index_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "%lu\n", __ring_index[mempool->type]);
	return 0;
}

static ssize_t debugfs_ring_index_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));
	unsigned int value = 0;

	if (!mempool)
		goto done;

	if (kstrtouint_from_user(buf, count, 0, &value))
		return -EFAULT;

	if (value >= mempool->ring.size)
		return -EINVAL;

	__ring_index[mempool->type] = value;
done:
	return count;
}
DEFINE_DEBUGFS_OPS(debugfs_ring_index, debugfs_ring_index_read,
		   debugfs_ring_index_write);

static int debugfs_ring_data_read(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool) {
		fsm_dp_ring_element_t *elem_p;

		elem_p =
			(mempool->ring.element + __ring_index[mempool->type]);

		seq_printf(s, "0x%lx\n", elem_p->element_data);
	}
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_ring_data, debugfs_ring_data_read, NULL);

static int debugfs_mempool_status_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool) {
		seq_printf(s, "BufPut:                 %lu\n",
			   mempool->stats.buf_put);
		seq_printf(s, "InvalidBufPut:            %lu\n",
			   mempool->stats.invalid_buf_put);
		seq_printf(s, "ErrBufPut:                %lu\n",
			   mempool->stats.buf_put_err);
		seq_printf(s, "BufGet:                   %lu\n",
			   mempool->stats.buf_get);
		seq_printf(s, "InvalidBufGet:            %lu\n",
			   mempool->stats.invalid_buf_get);
		seq_printf(s, "ErrBufGet:                %lu\n",
			   mempool->stats.buf_get_err);
		seq_printf(s, "DMA time exceed thrshold: %lu\n",
			   mempool->stats.buf_dma_exceed);
	}
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mempool_status, debugfs_mempool_status_show, NULL);

static int debugfs_mempool_state_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);
	unsigned long state_cnt[FSM_DP_BUF_STATE_LAST];
	unsigned long buf_bad = 0;
	unsigned long unknown_state = 0;
	int i;

	memset(state_cnt, 0, sizeof(state_cnt));
	if (mempool) {
		struct fsm_dp_mem *mem = &mempool->mem;
		struct fsm_dp_buf_cntrl *p;

		for (i = 0; i < mem->buf_cnt; i++) {
			p = (struct fsm_dp_buf_cntrl *)
				fsm_dp_mem_rec_addr(mem, i);
			if (p == NULL)
				return 0;
#ifdef FSM_DP_BUFFER_FENCING
			if (p->signature != FSM_DP_BUFFER_SIG ||
					p->fence != FSM_DP_BUFFER_FENCE_SIG ||
					p->buf_index != i)
				buf_bad++;
			else if (p->state >= FSM_DP_BUF_STATE_LAST)
#else
			if (p->state >= FSM_DP_BUF_STATE_LAST)
#endif
				unknown_state++;
			else
				state_cnt[p->state]++;

		}

		seq_printf(s, "Total Buf:                  %u\n",
			   mem->buf_cnt);
		seq_printf(s, "Buf Real Size:              %u\n",
			   mem->buf_sz + mem->buf_overhead_sz);
		seq_printf(s, "Buf Corrupted:              %lu\n",
			   buf_bad);
		seq_printf(s, "Buf Unknown State:          %lu\n",
			   unknown_state);

		for (i = 0; i < FSM_DP_BUF_STATE_LAST; i++) {
			if (state_cnt[i]) {
				seq_printf(s, "Buf State %s:        ",
						fsm_dp_buf_state_to_str(i));
				seq_printf(s, "                    %lu\n",
						state_cnt[i]);
			}
		}
	}
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mempool_state, debugfs_mempool_state_show, NULL);

static unsigned long calc_ts_diff_us(struct fsm_dp_timespec *end, struct fsm_dp_timespec *start)
{
	unsigned long diff = 0;

	diff = (end->tv_sec - start->tv_sec) * 1000000000;
	diff = (diff + end->tv_nsec) - start->tv_nsec;
	return diff / 1000;
}

void fsm_dp_register_dl_traffic(struct fsm_dp_mempool *mempool,
					struct fsm_dp_buf_cntrl *pf)
{
	unsigned long diff;
	unsigned int cur;
	unsigned int prev;

	if (mempool->pf_enable < 1)
		return;
	if (!pf->ts[FSM_DP_DL_APPL_SEND_REQ_INDEX].tv_sec ||
		!pf->ts[FSM_DP_DL_KERNEL_SEND_REQ_INDEX].tv_sec)
		return;
	mempool->dl_traffic_profiling.frame_count++;
	memcpy(&mempool->dl_traffic_profiling.
		entry[mempool->dl_traffic_profiling.next], &pf->ts[0],
		sizeof(struct traffic_profiling_entry));
	cur = mempool->dl_traffic_profiling.next;
	mempool->dl_traffic_profiling.next++;
	if (mempool->dl_traffic_profiling.next  >= NUM_DL_PROFILING) {
		mempool->dl_traffic_profiling.wrap = true;
		mempool->dl_traffic_profiling.next = 0;
	}

	diff = calc_ts_diff_us(
		&mempool->dl_traffic_profiling.entry[cur]
				.ts[FSM_DP_DL_KERNEL_SEND_REQ_INDEX],
		&mempool->dl_traffic_profiling.entry[cur]
				.ts[FSM_DP_DL_APPL_SEND_REQ_INDEX]);
	if (diff > mempool->dl_traffic_profiling.max_dma_req)
		mempool->dl_traffic_profiling.max_dma_req = diff;
	if (diff < mempool->dl_traffic_profiling.min_dma_req)
		mempool->dl_traffic_profiling.min_dma_req = diff;
	mempool->dl_traffic_profiling.avg_dma_req += diff;

	diff = calc_ts_diff_us(
		&mempool->dl_traffic_profiling.entry[cur]
				.ts[FSM_DP_DL_SEND_DMA_COMP_INDEX],
		&mempool->dl_traffic_profiling.entry[cur]
				.ts[FSM_DP_DL_KERNEL_SEND_REQ_INDEX]);

	if (diff > (mempool->dl_max_dma_cmplt_time * MILISEC)) {
		FSM_DP_WARN_RATELIMITED(
			"%s: tx DMA taking %ld micro second to complete\n",
                          __func__, diff);
		mempool->stats.buf_dma_exceed++;
	}
	if (diff > mempool->dl_traffic_profiling.max_dma_cmp)
		mempool->dl_traffic_profiling.max_dma_cmp = diff;
	if (diff < mempool->dl_traffic_profiling.min_dma_cmp)
		mempool->dl_traffic_profiling.min_dma_cmp = diff;
	mempool->dl_traffic_profiling.avg_dma_cmp += diff;

	if (mempool->dl_traffic_profiling.frame_count == 1)
		return;
	if (cur == 0)
		prev = NUM_DL_PROFILING - 1;
	else
		prev = cur - 1;

	diff = calc_ts_diff_us(
		&mempool->dl_traffic_profiling.
			entry[cur].ts[FSM_DP_DL_APPL_SEND_REQ_INDEX],
		&mempool->dl_traffic_profiling.
			entry[prev].ts[FSM_DP_DL_APPL_SEND_REQ_INDEX]);

	if (mempool->dl_ifg_threshold && diff > mempool->dl_ifg_threshold * MILISEC)
		FSM_DP_WARN_RATELIMITED(
			"%s: tx interfame gap %ld micro second exceeds"
			" threshold of %d micro second\n",
                          __func__, diff, mempool->dl_ifg_threshold * MILISEC);

	if (diff > mempool->dl_traffic_profiling.max_frame_gap)
		mempool->dl_traffic_profiling.max_frame_gap = diff;
	if (diff < mempool->dl_traffic_profiling.min_frame_gap)
		mempool->dl_traffic_profiling.min_frame_gap = diff;
	mempool->dl_traffic_profiling.avg_frame_gap += diff;
}

static int debugfs_mempool_DL_traffic_pf_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);
	unsigned int start, iter, count = 0;
	unsigned long diff;

	if (!mempool || mempool->pf_enable < 1 ||
			!mempool->dl_traffic_profiling.frame_count)
		return 0;

	seq_printf(s, "Max transfer request time %ld us,"
		" Min transfer request time %ld us, ",
		mempool->dl_traffic_profiling.max_dma_req,
		mempool->dl_traffic_profiling.min_dma_req);
	seq_printf(s, " Average transfer request time %ld.%-3ld us\n",
		 mempool->dl_traffic_profiling.avg_dma_req /
			mempool->dl_traffic_profiling.frame_count,
		(mempool->dl_traffic_profiling.avg_dma_req %
			mempool->dl_traffic_profiling.frame_count) * 100 /
			mempool->dl_traffic_profiling.frame_count);
	seq_printf(s, "Max transfer complete time %ld us,"
		" Min transfer complete time %ld us,",
		mempool->dl_traffic_profiling.max_dma_cmp,
		mempool->dl_traffic_profiling.min_dma_cmp);
	seq_printf(s, " Average transfer complete time %ld.%-3ld us\n",
		mempool->dl_traffic_profiling.avg_dma_cmp /
			mempool->dl_traffic_profiling.frame_count,
		(mempool->dl_traffic_profiling.avg_dma_cmp %
			mempool->dl_traffic_profiling.frame_count) * 100 /
			mempool->dl_traffic_profiling.frame_count);
	if (mempool->dl_traffic_profiling.frame_count > 1) {
		seq_printf(s, "Max inter frame gap time %ld us,"
			" Min inter frame gap time %ld us, ",
			mempool->dl_traffic_profiling.max_frame_gap,
			mempool->dl_traffic_profiling.min_frame_gap);
		seq_printf(s,
			" Average inter frame gap time %ld.%-3ld us\n",
			mempool->dl_traffic_profiling.avg_frame_gap /
				mempool->dl_traffic_profiling.frame_count - 1,
			(mempool->dl_traffic_profiling.avg_frame_gap %
				mempool->dl_traffic_profiling.frame_count - 1)
					* 100 /
				mempool->dl_traffic_profiling.frame_count - 1);
	}
	if (mempool->pf_enable ==  1)
		return 0;
	if (!mempool->dl_traffic_profiling.wrap)
		start = 0;
	else
		start = mempool->dl_traffic_profiling.next;
	for (iter  = start;;) {
		struct traffic_profiling_entry *p;
		struct fsm_dp_timespec prev;
		struct  fsm_dp_timespec *pp = NULL;

		p = &mempool->dl_traffic_profiling.entry[iter];
		if (pp)
			diff = calc_ts_diff_us(
					&p->ts[FSM_DP_DL_APPL_SEND_REQ_INDEX],
					pp);
		seq_printf(s,	"Req %dth entry: "
				"Apps Transfer Req time %lld.%ld,"
				"Kernel Transfer Req time %lld.%ld,"
				"Transfer Cmp time %lld.%ld, ", count,
			p->ts[FSM_DP_DL_APPL_SEND_REQ_INDEX].tv_sec,
			p->ts[FSM_DP_DL_APPL_SEND_REQ_INDEX].tv_nsec,
			p->ts[FSM_DP_DL_KERNEL_SEND_REQ_INDEX].tv_sec,
			p->ts[FSM_DP_DL_KERNEL_SEND_REQ_INDEX].tv_nsec,
			p->ts[FSM_DP_DL_SEND_DMA_COMP_INDEX].tv_sec,
			p->ts[FSM_DP_DL_SEND_DMA_COMP_INDEX].tv_nsec);
		if (pp)
			seq_printf(s, "Inter Frame Gap %ldus\n", diff);
		else
			seq_printf(s, "\n");
		prev = mempool->dl_traffic_profiling.entry[iter]
				.ts[FSM_DP_DL_APPL_SEND_REQ_INDEX];
		pp = &prev;
		iter++;
		if (!mempool->dl_traffic_profiling.wrap) {
			if (iter == mempool->dl_traffic_profiling.frame_count)
				break;
		} else if (iter == NUM_DL_PROFILING)
			iter = 0;
		count++;
		if (iter == start)
			break;
	}
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mempool_DL_traffic_pf,
		debugfs_mempool_DL_traffic_pf_show, NULL);

static int debugfs_mempool_traffic_pf_enable_read(struct seq_file *s,
	void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "%s\n", (mempool->pf_enable) ? "enable" : "disable");
	return 0;
}

static ssize_t debugfs_mempool_traffic_pf_enable_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));
	unsigned int value = 0;

	if (!mempool)
		return -EINVAL;

	if (kstrtouint_from_user(buf, count, 0, &value))
		return -EFAULT;
	mempool->pf_enable = value;
	mempool_traffic_pf_reset(mempool);
	return count;
}

DEFINE_DEBUGFS_OPS(debugfs_mempool_traffic_pf_enable,
	debugfs_mempool_traffic_pf_enable_read,
	debugfs_mempool_traffic_pf_enable_write);

static int debugfs_mempool_DL_ifg_threshold_read(struct seq_file *s,
	void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "DL Inter Frame Gap Threshold %d mili second\n",
			mempool->dl_ifg_threshold);
	return 0;
}

static ssize_t debugfs_mempool_DL_ifg_threshold_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));
	unsigned int value = 0;

	if (!mempool)
		return -EINVAL;

	if (kstrtouint_from_user(buf, count, 0, &value))
		return -EFAULT;
	mempool->dl_ifg_threshold = value;
	return count;
}

DEFINE_DEBUGFS_OPS(debugfs_mempool_DL_ifg_threshold,
	debugfs_mempool_DL_ifg_threshold_read,
	debugfs_mempool_DL_ifg_threshold_write);

static int debugfs_mempool_DL_max_dma_read(struct seq_file *s,
	void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool)
		seq_printf(s, "DL Max DMA Complete Time: %d mili second\n",
			mempool->dl_max_dma_cmplt_time);
	return 0;
}

static ssize_t debugfs_mempool_DL_max_dma_write(
	struct file *fp,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct fsm_dp_mempool *mempool = *((struct fsm_dp_mempool **)
			(((struct seq_file *)fp->private_data)->private));
	unsigned int value = 0;

	if (!mempool)
		return -EINVAL;

	if (kstrtouint_from_user(buf, count, 0, &value))
		return -EFAULT;
	mempool->dl_max_dma_cmplt_time = value;
	return count;
}

DEFINE_DEBUGFS_OPS(debugfs_mempool_DL_max_dma,
	debugfs_mempool_DL_max_dma_read,
	debugfs_mempool_DL_max_dma_write);


static int debugfs_mempool_active_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_drv *drv = (struct fsm_dp_drv *)s->private;
	unsigned int type;

	for (type = 0; type < FSM_DP_MEM_TYPE_LAST; type++) {
		if (drv->mempool[type])
			seq_printf(s, "%s ", fsm_dp_mem_type_to_str(type));
	}
	seq_puts(s, "\n");
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mempool_active, debugfs_mempool_active_show, NULL);

static int debugfs_mempool_info_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_mempool *mempool =
		*((struct fsm_dp_mempool **)s->private);

	if (mempool) {
		seq_printf(s, "Driver:                 %llx\n",
							(u64) mempool->drv);
		seq_printf(s, "MemPool:                %llx\n",
							(u64) mempool);
		seq_printf(s, "Type:                   %s\n",
			   fsm_dp_mem_type_to_str(mempool->type));
		seq_printf(s, "Ref:                    %d\n",
			   atomic_read(&mempool->ref));
	}
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mempool_info, debugfs_mempool_info_show, NULL);

static int debugfs_mhi_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_drv *drv = (struct fsm_dp_drv *)s->private;
	struct fsm_dp_mhi *mhi = &drv->mhi;

	seq_printf(s, "MHIDevice:              %llx\n", (u64) mhi->mhi_dev);
	seq_puts(s, "  Stats:\n");
	seq_printf(s, "    TX:                 %lu\n", mhi->stats.tx_cnt);
	seq_printf(s, "    TX_ACKED:           %lu\n", mhi->stats.tx_acked);
	seq_printf(s, "    TX_ERR:             %lu\n", mhi->stats.tx_err);
	seq_printf(s, "    RX:                 %lu\n", mhi->stats.rx_cnt);
	seq_printf(s, "    RX_ERR:             %lu\n", mhi->stats.rx_err);
	seq_printf(s, "    RX_OUT_OF_BUF:      %lu\n", mhi->stats.rx_out_of_buf);
	seq_printf(s, "    RX_REPLENISH:       %lu\n", mhi->stats.rx_replenish);
	seq_printf(s, "    RX_REPLENISH_ERR:   %lu\n", mhi->stats.rx_replenish_err);
	seq_printf(s, "    RX_OUTOFBUF_DROP:   %lu\n", mhi->stats.rx_outofbuf_drop);
	seq_printf(s, "    RX_OUTOFBUF_RESYNC: %lu\n", mhi->stats.rx_resync);

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_mhi, debugfs_mhi_show, NULL);

static int debugfs_cdev_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_drv *drv = (struct fsm_dp_drv *)s->private;
	struct fsm_dp_cdev *cdev;
	int n = 0;
	int i;

	mutex_lock(&drv->cdev_lock);
	list_for_each_entry(cdev, &drv->cdev_head, list) {
		seq_printf(s, "CDEV(%d)\n", n++);
		seq_printf(s, "Driver:                 %llx\n",
							(u64) cdev->pdrv);
		seq_printf(s, "Cdev:                   %llx\n",
							(u64) cdev);
		seq_printf(s, "PID:                    %d\n",
							cdev->pid);
		seq_printf(s, "TX_Mode:                %d\n",
							cdev->tx_mode);

		for (i = 0; i < FSM_DP_MEM_TYPE_LAST; i++) {
			seq_printf(s, "MemPoolVMA[%d]\n", i);
			__fsm_dp_mempool_vma_dump(s, &cdev->mempool_vma[i]);
		}
		seq_puts(s, "RxQueue\n");
		for (i = 0; i < FSM_DP_RX_TYPE_LAST; i++)
			__fsm_dp_rxqueue_vma_dump(s, &cdev->rxqueue_vma[i]);
	}
	mutex_unlock(&drv->cdev_lock);

	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_cdev, debugfs_cdev_show, NULL);

static int debugfs_drv_status_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_drv *drv = (struct fsm_dp_drv *)s->private;
	struct fsm_dp_core_stats *stats = &drv->stats;

	seq_printf(s, "TX:             %lu\n", stats->tx_cnt);
	seq_printf(s, "TX_ERR:         %lu\n", stats->tx_err);
	seq_printf(s, "RX:             %lu\n", stats->rx_cnt);
	seq_printf(s, "RX_BADMSG:      %lu\n", stats->rx_badmsg);
	seq_printf(s, "RX_DROP:        %lu\n", stats->rx_drop);
	seq_printf(s, "RX_INT:         %lu\n", stats->rx_int);
	seq_printf(s, "RX_BUDGET_OVF:  %lu\n", stats->rx_budget_overflow);
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_drv_status, debugfs_drv_status_show, NULL);

static int debugfs_drv_show(struct seq_file *s, void *unused)
{
	struct fsm_dp_drv *drv = (struct fsm_dp_drv *)s->private;
	struct platform_device *pdev = to_platform_device(drv->dev);

	seq_printf(s, "Driver:         %llx\n", (u64) drv);
	seq_printf(s, "Name:           %s\n", pdev->name);
	return 0;
}
DEFINE_DEBUGFS_OPS(debugfs_drv, debugfs_drv_show, NULL);

static ssize_t fsm_dp_log_level_select(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	fsm_log_level_t log_level;
	if (kstrtouint_from_user(buf, count, 0, &log_level))
		return -EFAULT;

	if (log_level <= FSM_LOG_LEVEL_DEBUG &&
			log_level >= FSM_LOG_LEVEL_EMERG) {
		fsm_dp_log_level = log_level;
		FSM_DP_DEBUG("loglevel: %d\n", fsm_dp_log_level);
	}
	return count;
}

static const struct file_operations fsm_dp_log_level_ops = {
	.open = fsm_log_level_open,
	.release = single_release,
	.read = seq_read,
	.write = fsm_dp_log_level_select,
};
static int debugfs_create_loopback_dir(struct dentry *parent,
				       struct fsm_dp_drv *drv)
{
	struct dentry *entry = NULL, *dentry = NULL;

	dentry = debugfs_create_dir("loopback", parent);
	if (IS_ERR(dentry))
		return -ENOMEM;

	entry = debugfs_create_file("status", 0444, dentry,
				    &drv->loopback,
				    &debugfs_loopback_ops);
	if (!entry)
		return -ENOMEM;
	return 0;
}

static int debugfs_create_rxq_dir(struct dentry *parent, struct fsm_dp_drv *drv)
{
	struct dentry *entry = NULL, *dentry = NULL, *root = NULL;
	unsigned int type;

	root = debugfs_create_dir("rxque", parent);
	if (IS_ERR(root))
		return -ENOMEM;

	for (type = 0; type < FSM_DP_RX_TYPE_LAST; type++) {
		dentry = debugfs_create_dir(fsm_dp_rx_type_to_str(type),
					    root);
		if (IS_ERR(dentry))
			return -ENOMEM;

		entry = debugfs_create_file("config", 0444, dentry,
					    &drv->rxq[type],
					    &debugfs_rxq_config_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("runtime", 0444, dentry,
					    &drv->rxq[type],
					    &debugfs_rxq_runtime_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("opstats", 0444, dentry,
					    &drv->rxq[type],
					    &debugfs_rxq_opstats_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("refcnt", 0444, dentry,
					    &drv->rxq[type],
					    &debugfs_rxq_refcnt_ops);
		if (!entry)
			return -ENOMEM;
	}
	return 0;
}

static int debugfs_create_ring_dir(
	struct dentry *parent,
	struct fsm_dp_mempool **mempool)
{
	struct dentry *entry = NULL, *dentry = NULL;

	dentry = debugfs_create_dir("ring", parent);
	if (IS_ERR(dentry))
		return -ENOMEM;

	entry = debugfs_create_file("config", 0444, dentry,
				    mempool,
				    &debugfs_ring_config_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("runtime", 0444, dentry,
				    mempool,
				    &debugfs_ring_runtime_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("index", 0644, dentry,
				    mempool,
				    &debugfs_ring_index_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("data", 0644, dentry,
				    mempool,
				    &debugfs_ring_data_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("opstats", 0444, dentry,
				    mempool,
				    &debugfs_ring_opstats_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static int debugfs_create_mem_dir(
	struct dentry *parent,
	struct fsm_dp_mempool **mempool)
{
	struct dentry *entry = NULL, *dentry = NULL;

	dentry = debugfs_create_dir("mem", parent);
	if (IS_ERR(dentry))
		return -ENOMEM;

	entry = debugfs_create_file("config", 0444, dentry,
				    mempool,
				    &debugfs_mem_config_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("offset", 0444, dentry,
				    mempool,
				    &debugfs_mem_offset_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("dump_size", 0644, dentry,
				    mempool,
				    &debugfs_mem_dump_size_ops);
	if (!entry)
		return -ENOMEM;

	entry = debugfs_create_file("data", 0644, dentry,
				    mempool,
				    &debugfs_mem_data_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static int debugfs_create_mempool_dir(
	struct dentry *parent,
	struct fsm_dp_drv *drv)
{
	struct dentry *entry = NULL, *dentry = NULL, *root = NULL;
	int ret;
	unsigned int type;

	root = debugfs_create_dir("mempool", parent);
	if (IS_ERR(root))
		return -ENOMEM;

	entry = debugfs_create_file("active", 0444, root, drv,
				    &debugfs_mempool_active_ops);
	if (!entry)
		return -ENOMEM;

	for (type = 0; type < FSM_DP_MEM_TYPE_LAST; type++) {
		dentry = debugfs_create_dir(fsm_dp_mem_type_to_str(type), root);
		if (IS_ERR(dentry))
			return -ENOMEM;

		ret = debugfs_create_ring_dir(dentry, &drv->mempool[type]);
		if (ret)
			return ret;

		ret = debugfs_create_mem_dir(dentry, &drv->mempool[type]);
		if (ret)
			return ret;

		entry = debugfs_create_file("info", 0444, dentry,
					    &drv->mempool[type],
					    &debugfs_mempool_info_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("status", 0444, dentry,
					    &drv->mempool[type],
					    &debugfs_mempool_status_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("state", 0444, dentry,
					    &drv->mempool[type],
					    &debugfs_mempool_state_ops);
		if (!entry)
			return -ENOMEM;

		entry = debugfs_create_file("traffic_profiling_enable_level", 0444,
				dentry,
				&drv->mempool[type],
				&debugfs_mempool_traffic_pf_enable_ops);
		if (!entry)
			return -ENOMEM;
		if (type != FSM_DP_MEM_TYPE_UL) {
			entry = debugfs_create_file("DL_traffic_profiling",
				0444, dentry, &drv->mempool[type],
				&debugfs_mempool_DL_traffic_pf_ops);
			if (!entry)
				return -ENOMEM;

			entry = debugfs_create_file("DL_inter_frame_gap_threshold",
				0444, dentry, &drv->mempool[type],
				&debugfs_mempool_DL_ifg_threshold_ops);
			if (!entry)
				return -ENOMEM;

			entry = debugfs_create_file("DL_max_dma_req",
				0444, dentry, &drv->mempool[type],
				&debugfs_mempool_DL_max_dma_ops);
			if (!entry)
				return -ENOMEM;
		}
	}
	return 0;
}

int fsm_dp_debugfs_init(struct fsm_dp_drv *drv)
{
	struct dentry *entry = NULL;

	if (unlikely(drv == NULL))
		return -EINVAL;

	if (unlikely(__dent))
		return -EBUSY;

	__dent = debugfs_create_dir(FSM_DP_MODULE_NAME, 0);
	if (IS_ERR(__dent))
		return -ENOMEM;

	entry = debugfs_create_file("driver", 0444, __dent, drv,
				    &debugfs_drv_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("cdev", 0444, __dent, drv,
				    &debugfs_cdev_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("mhi", 0444, __dent, drv,
				    &debugfs_mhi_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("status", 0444, __dent, drv,
				    &debugfs_drv_status_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("log_level", 0664, __dent, NULL,
				    &fsm_dp_log_level_ops);
	if (!entry)
		goto err;

	if (debugfs_create_mempool_dir(__dent, drv))
		goto err;

	if (debugfs_create_rxq_dir(__dent, drv))
		goto err;

	if (debugfs_create_loopback_dir(__dent, drv))
		goto err;

	return 0;
err:
	debugfs_remove_recursive(__dent);
	__dent = NULL;
	return -ENOMEM;
}

void fsm_dp_debugfs_cleanup(struct fsm_dp_drv *drv)
{
	debugfs_remove_recursive(__dent);
	__dent = NULL;
}

#else

int fsm_dp_debugfs_init(struct fsm_dp_drv *drv)
{
	return 0;
}

void fsm_dp_debugfs_cleanup(struct fsm_dp_drv *drv)
{
}
#endif

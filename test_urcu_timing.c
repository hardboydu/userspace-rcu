/*
 * test_urcu.c
 *
 * Userspace RCU library - test program
 *
 * Copyright February 2009 - Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/syscall.h>
#include <arch.h>

#if defined(_syscall0)
_syscall0(pid_t, gettid)
#elif defined(__NR_gettid)
static inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}
#else
#warning "use pid as tid"
static inline pid_t gettid(void)
{
	return getpid();
}
#endif

#define _LGPL_SOURCE
#include "urcu.h"

pthread_mutex_t rcu_copy_mutex = PTHREAD_MUTEX_INITIALIZER;

void rcu_copy_mutex_lock(void)
{
	int ret;
	ret = pthread_mutex_lock(&rcu_copy_mutex);
	if (ret) {
		perror("Error in pthread mutex lock");
		exit(-1);
	}
}

void rcu_copy_mutex_unlock(void)
{
	int ret;

	ret = pthread_mutex_unlock(&rcu_copy_mutex);
	if (ret) {
		perror("Error in pthread mutex unlock");
		exit(-1);
	}
}

struct test_array {
	int a;
};

static struct test_array *test_rcu_pointer;

#define OUTER_READ_LOOP	2000U
#define INNER_READ_LOOP	100000U
#define READ_LOOP ((unsigned long long)OUTER_READ_LOOP * INNER_READ_LOOP)

#define OUTER_WRITE_LOOP 10U
#define INNER_WRITE_LOOP 200U
#define WRITE_LOOP ((unsigned long long)OUTER_WRITE_LOOP * INNER_WRITE_LOOP)

#define NR_READ 10
#define NR_WRITE 9

static cycles_t reader_time[NR_READ] __attribute__((aligned(128)));
static cycles_t writer_time[NR_WRITE] __attribute__((aligned(128)));

void *thr_reader(void *arg)
{
	int i, j;
	struct test_array *local_ptr;
	cycles_t time1, time2;

	printf("thread_begin %s, thread id : %lx, tid %lu\n",
			"reader", pthread_self(), (unsigned long)gettid());
	sleep(2);

	rcu_register_thread();

	time1 = get_cycles();
	for (i = 0; i < OUTER_READ_LOOP; i++) {
		for (j = 0; j < INNER_READ_LOOP; j++) {
			rcu_read_lock();
			local_ptr = rcu_dereference(test_rcu_pointer);
			if (local_ptr) {
				assert(local_ptr->a == 8);
			}
			rcu_read_unlock();
		}
	}
	time2 = get_cycles();

	rcu_unregister_thread();

	reader_time[(unsigned long)arg] = time2 - time1;

	sleep(2);
	printf("thread_end %s, thread id : %lx, tid %lu\n",
			"reader", pthread_self(), (unsigned long)gettid());
	return ((void*)1);

}

void *thr_writer(void *arg)
{
	int i, j;
	struct test_array *new, *old;
	cycles_t time1, time2;

	printf("thread_begin %s, thread id : %lx, tid %lu\n",
			"writer", pthread_self(), (unsigned long)gettid());
	sleep(2);

	for (i = 0; i < OUTER_WRITE_LOOP; i++) {
		for (j = 0; j < INNER_WRITE_LOOP; j++) {
			time1 = get_cycles();
			new = malloc(sizeof(struct test_array));
			rcu_copy_mutex_lock();
			old = test_rcu_pointer;
			if (old) {
				assert(old->a == 8);
			}
			new->a = 8;
			old = rcu_publish_content(&test_rcu_pointer, new);
			rcu_copy_mutex_unlock();
			/* can be done after unlock */
			if (old) {
				old->a = 0;
			}
			free(old);
			time2 = get_cycles();
			writer_time[(unsigned long)arg] += time2 - time1;
			usleep(1);
		}
	}

	printf("thread_end %s, thread id : %lx, tid %lu\n",
			"writer", pthread_self(), (unsigned long)gettid());
	return ((void*)2);
}

int main()
{
	int err;
	pthread_t tid_reader[NR_READ], tid_writer[NR_WRITE];
	void *tret;
	int i;
	cycles_t tot_rtime = 0;
	cycles_t tot_wtime = 0;

	printf("thread %-6s, thread id : %lx, tid %lu\n",
			"main", pthread_self(), (unsigned long)gettid());

	for (i = 0; i < NR_READ; i++) {
		err = pthread_create(&tid_reader[i], NULL, thr_reader,
				     (void *)(long)i);
		if (err != 0)
			exit(1);
	}
	for (i = 0; i < NR_WRITE; i++) {
		err = pthread_create(&tid_writer[i], NULL, thr_writer,
				     (void *)(long)i);
		if (err != 0)
			exit(1);
	}

	sleep(10);

	for (i = 0; i < NR_READ; i++) {
		err = pthread_join(tid_reader[i], &tret);
		if (err != 0)
			exit(1);
		tot_rtime += reader_time[i];
	}
	for (i = 0; i < NR_WRITE; i++) {
		err = pthread_join(tid_writer[i], &tret);
		if (err != 0)
			exit(1);
		tot_wtime += writer_time[i];
	}
	free(test_rcu_pointer);
	printf("Time per read : %g cycles\n",
	       (double)tot_rtime / ((double)NR_READ * (double)READ_LOOP));
	printf("Time per write : %g cycles\n",
	       (double)tot_wtime / ((double)NR_WRITE * (double)WRITE_LOOP));

	return 0;
}

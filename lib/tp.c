#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "../smalloc.h"
#include "../log.h"
#include "tp.h"

static void tp_flush_work(struct flist_head *list)
{
	struct tp_work *work;

	while (!flist_empty(list)) {
		work = flist_entry(list->next, struct tp_work, list);
		flist_del(&work->list);
		work->fn(work);
	}
}

static void *tp_thread(void *data)
{
	struct tp_data *tdat = data;
	struct flist_head work_list;

	INIT_FLIST_HEAD(&work_list);

	while (1) {
		pthread_mutex_lock(&tdat->lock);

		if (!tdat->thread_exit && flist_empty(&tdat->work))
			pthread_cond_wait(&tdat->cv, &tdat->lock);

		if (!flist_empty(&tdat->work))
			flist_splice_tail_init(&tdat->work, &work_list);

		pthread_mutex_unlock(&tdat->lock);

		if (flist_empty(&work_list)) {
			if (tdat->thread_exit)
				break;
			continue;
		}

		tp_flush_work(&work_list);
	}

	return NULL;
}

void tp_queue_work(struct tp_data *tdat, struct tp_work *work)
{
	work->done = 0;

	pthread_mutex_lock(&tdat->lock);
	flist_add_tail(&work->list, &tdat->work);
	pthread_cond_signal(&tdat->cv);
	pthread_mutex_unlock(&tdat->lock);
}

void tp_init(struct tp_data **tdatp)
{
	struct tp_data *tdat;
	int ret;

	if (*tdatp)
		return;

	*tdatp = tdat = smalloc(sizeof(*tdat));
	pthread_mutex_init(&tdat->lock, NULL);
	INIT_FLIST_HEAD(&tdat->work);
	pthread_cond_init(&tdat->cv, NULL);
	pthread_cond_init(&tdat->sleep_cv, NULL);

	ret = pthread_create(&tdat->thread, NULL, tp_thread, tdat);
	if (ret)
		log_err("fio: failed to create tp thread\n");
}

void tp_exit(struct tp_data **tdatp)
{
	struct tp_data *tdat = *tdatp;
	void *ret;

	if (!tdat)
		return;

	tdat->thread_exit = 1;
	pthread_mutex_lock(&tdat->lock);
	pthread_cond_signal(&tdat->cv);
	pthread_mutex_unlock(&tdat->lock);

	pthread_join(tdat->thread, &ret);

	sfree(tdat);
	*tdatp = NULL;
}

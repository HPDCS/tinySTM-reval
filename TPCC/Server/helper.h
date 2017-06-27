/*
 * helper.h
 *
 *  Created on: 06/ott/2012
 *      Author: Alessandro
 */

#ifndef HELPER_H_
#define HELPER_H_


/*
 * define new marco for TM transaction
 * TM_BEGIN_CUSTOM()
 * start the transaction with this marco if you want not restart the transaction when TM_ABORT() is invoked
 *
 * TM_ABORT()
 * Abort the transaction
 */

#	define RO				1
#	define RW				0
#	define NO_RETRY			1
#	define RETRY			0

#	define ID_NWORDER		0
#	define ID_PAYMENT		1
#	define ID_ORDSTATUS 	2
#	define ID_STOCKLEVEL	3
#	define ID_DELIVERY		4


/*
#   define TM_BEGIN_CUSTOM(id,rw,no_retry)                { stm_tx_attr_t _a = {id, rw, no_retry}; sigjmp_buf *_e = stm_start(&_a); if (_e != NULL) sigsetjmp(*_e, 0);}
*/


#	define TM_ABORT()	do { \
						    stm_abort(0); \
						} while(0)

#endif /* HELPER_H_ */

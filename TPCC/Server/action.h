#ifndef ACTION_H
#define ACTION_H 1


typedef enum action {
    NEWORDER_TRANSACTION = 0,
    PAYMENT_TRANSACTION  = 1,
    ORDERSTATUS_TRANSACTION   = 2,
    DELIVERY_TRANSACTION =3,
    STOCKLEVEL_TRANSACTION =4,

} action_t;


#endif /* ACTION_H */


/* =============================================================================
 *
 * End of action.h
 *
 * =============================================================================
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <sys/file.h>


#include "ngx_dynamic_upstream_module.h"
#include "ngx_inet_slab.h"


static const ngx_str_t ngx_dynamic_upstream_params[] = {
    ngx_string("arg_upstream"),
    ngx_string("arg_verbose"),
    ngx_string("arg_add"),
    ngx_string("arg_remove"),
    ngx_string("arg_backup"),
    ngx_string("arg_server"),
    ngx_string("arg_weight"),
    ngx_string("arg_max_fails"),
    ngx_string("arg_fail_timeout"),
    ngx_string("arg_up"),
    ngx_string("arg_down")
};


static ngx_int_t
ngx_dynamic_upstream_is_shpool_range(ngx_http_request_t *r,ngx_slab_pool_t *shpool, void *p);
static ngx_int_t
ngx_dynamic_upstream_op_add(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                            ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf);
static ngx_int_t
ngx_dynamic_upstream_op_remove(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                               ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf);
static ngx_int_t
ngx_dynamic_upstream_op_update_param(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                                     ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf);


static ngx_int_t
ngx_dynamic_upstream_is_shpool_range(ngx_http_request_t *r,ngx_slab_pool_t *shpool, void *p)
{
    if ((u_char *) p < shpool->start || (u_char *) p > shpool->end) {
        return 0;
    }

    return 1;
}


ngx_int_t
ngx_dynamic_upstream_build_op(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op)
{
    ngx_uint_t                  i;
    size_t                      args_size;
    u_char                     *low;
    ngx_uint_t                  key;
    ngx_str_t                  *args;
    ngx_http_variable_value_t  *var;

    ngx_memzero(op, sizeof(ngx_dynamic_upstream_op_t));

    /* default setting for op */
    op->op = NGX_DYNAMIC_UPSTEAM_OP_LIST;
    op->status = NGX_HTTP_OK;
    ngx_str_null(&op->upstream);
    op->weight       = 1;
    op->max_fails    = 1;
    op->fail_timeout = 10;

    args = (ngx_str_t *)&ngx_dynamic_upstream_params;
    args_size = sizeof(ngx_dynamic_upstream_params) / sizeof(ngx_str_t);
    for (i=0;i<args_size;i++) {
        low = ngx_pnalloc(r->pool, args[i].len);
        if (low == NULL) {
            op->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to allocate memory from r->pool %s:%d",
                          __FUNCTION__,
                          __LINE__);
            return NGX_ERROR;
        }

        key = ngx_hash_strlow(low, args[i].data, args[i].len);
        var = ngx_http_get_variable(r, &args[i], key);

        if (!var->not_found) {
            if (ngx_strcmp("arg_upstream", args[i].data) == 0) {
                op->upstream.data = var->data;
                op->upstream.len = var->len;

            } else if (ngx_strcmp("arg_verbose", args[i].data) == 0) {
                op->verbose = 1;

            } else if (ngx_strcmp("arg_add", args[i].data) == 0) {
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_ADD;

            } else if (ngx_strcmp("arg_remove", args[i].data) == 0) {
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_REMOVE;

            } else if (ngx_strcmp("arg_backup", args[i].data) == 0) {
                op->backup = 1;

            } else if (ngx_strcmp("arg_server", args[i].data) == 0) {
                op->server.data = var->data;
                op->server.len = var->len;

            } else if (ngx_strcmp("arg_weight", args[i].data) == 0) {
                op->weight = ngx_atoi(var->data, var->len);
                if (op->weight == NGX_ERROR) {
                    op->status = NGX_HTTP_BAD_REQUEST;
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "weight is not number. %s:%d",
                                  __FUNCTION__,
                                  __LINE__);
                    return NGX_ERROR;
                }
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_PARAM;
                op->op_param |= NGX_DYNAMIC_UPSTEAM_OP_PARAM_WEIGHT;
                op->verbose = 1;

            } else if (ngx_strcmp("arg_max_fails", args[i].data) == 0) {
                op->max_fails = ngx_atoi(var->data, var->len);
                if (op->max_fails == NGX_ERROR) {
                    op->status = NGX_HTTP_BAD_REQUEST;
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "max_fails is not number. %s:%d",
                                  __FUNCTION__,
                                  __LINE__);
                    return NGX_ERROR;
                }
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_PARAM;
                op->op_param |= NGX_DYNAMIC_UPSTEAM_OP_PARAM_MAX_FAILS;
                op->verbose = 1;

            } else if (ngx_strcmp("arg_fail_timeout", args[i].data) == 0) {
                op->fail_timeout = ngx_atoi(var->data, var->len);
                if (op->fail_timeout == NGX_ERROR) {
                    op->status = NGX_HTTP_BAD_REQUEST;
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "fail_timeout is not number. %s:%d",
                                  __FUNCTION__,
                                  __LINE__);
                    return NGX_ERROR;
                }
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_PARAM;
                op->op_param |= NGX_DYNAMIC_UPSTEAM_OP_PARAM_FAIL_TIMEOUT;
                op->verbose = 1;

            } else if (ngx_strcmp("arg_up", args[i].data) == 0) {
                op->up = 1;
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_PARAM;
                op->op_param |= NGX_DYNAMIC_UPSTEAM_OP_PARAM_UP;
                op->verbose = 1;

            } else if (ngx_strcmp("arg_down", args[i].data) == 0) {
                op->down = 1;
                op->op |= NGX_DYNAMIC_UPSTEAM_OP_PARAM;
                op->op_param |= NGX_DYNAMIC_UPSTEAM_OP_PARAM_DOWN;
                op->verbose = 1;
                
            }
        }
    }

    /* can not add and remove at once */
    if ((op->op & NGX_DYNAMIC_UPSTEAM_OP_ADD) &&
        (op->op & NGX_DYNAMIC_UPSTEAM_OP_REMOVE))
    {
        op->status = NGX_HTTP_BAD_REQUEST;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "add and remove at once are not allowed. %s:%d",
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }

    if (op->op & NGX_DYNAMIC_UPSTEAM_OP_ADD) {
        op->op = NGX_DYNAMIC_UPSTEAM_OP_ADD;
    } else if (op->op & NGX_DYNAMIC_UPSTEAM_OP_REMOVE) {
        op->op = NGX_DYNAMIC_UPSTEAM_OP_REMOVE;
    }

    /* can not up and down at once */
    if (op->up && op->down) {
        op->status = NGX_HTTP_BAD_REQUEST;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "down and up at once are not allowed. %s:%d",
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_dynamic_upstream_op(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                        ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_int_t  rc;

    rc = NGX_OK;

    switch (op->op) {
    case NGX_DYNAMIC_UPSTEAM_OP_ADD:
        rc = ngx_dynamic_upstream_op_add(r, op, shpool, uscf);
        break;
    case NGX_DYNAMIC_UPSTEAM_OP_REMOVE:
        rc = ngx_dynamic_upstream_op_remove(r, op, shpool, uscf);
        break;
#if 0
    case NGX_DYNAMIC_UPSTEAM_OP_BACKUP:
        break;
#endif
    case NGX_DYNAMIC_UPSTEAM_OP_PARAM:
        rc = ngx_dynamic_upstream_op_update_param(r, op, shpool, uscf);
        break;
    case NGX_DYNAMIC_UPSTEAM_OP_LIST:
    default:
        rc = NGX_OK;
        break;
    }

    return rc;
}

int strloc(char str1[],char str2[])
{
    if (strstr(str1,str2)==NULL)//找出字符串str1首次出现在str2的位置，如果找不到，返回空指针~
        return (-1);
    else 
        *(strstr(str1,str2)+1)='\0';//当然，这里已经动了str2的字符串了，如果不想动原来的，可以另外用一个str3来复制str2
    return (strlen(str1));
}

int strloc2(char str1[],int length,char ch)
{
   int i=0;
   for(;i<length;i++){
       if(str1[i]==ch)return i;
   }
   return -1;
}


static ngx_int_t
ngx_dynamic_upstream_op_add(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                            ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_http_upstream_rr_peer_t   *peer, *last;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_url_t                      u;

    // ----------------------add begin---------------------------------
    char * cfg_file = (char *)(uscf->file_name);
    ssize_t           n;
    ngx_fd_t          fd;
    fd = ngx_open_file("//opt//add.txt", NGX_FILE_WRONLY, NGX_FILE_CREATE_OR_OPEN,NGX_FILE_OWNER_ACCESS);
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "xxxxxxxxxxxxxxxxxxx.");
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                      ngx_open_file_n "//opt//add.txt  failed");
    }
    char msg[2000];
    strcpy(msg,"op_add:");
    char server_data[200];
    strncpy(server_data,(char*)(op->server.data),25);
    //int index=strchr(server_data,"HTTP");
    int index=strloc(server_data,"HTTP");
    //ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"index: %d",index);
    char serverd[50];
    char serverd2[80];
    strncpy(serverd,(char*)(op->server.data),index-2);
    strcpy(serverd2,"        server ");
    strcat(serverd2,serverd);
    strcat(serverd2,";\n");
    strncpy(server_data,(char*)(op->upstream.data),25);
    //index=strloc(server_data,200,"&add");
    index=strloc2(server_data,200,'&');
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"index: %d",index);
    char upstreamd[50];
    strncpy(upstreamd,(char*)(op->upstream.data),index-1);
    char dst[index-2];
    strncpy(dst,upstreamd,index-3);
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:dst %s %s iiiiiiiiii",serverd,dst);
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:server_data %s|%s|%s|%s9999999999999",serverd,upstreamd,op->server.data,op->upstream.data);
 
    FILE * pFile;
    FILE * pf;
    char * tmp_file="//tmp//nginx.conf";
    pFile=fopen(cfg_file,"r");
    pf=fopen(tmp_file,"w");
    flock(pf->_fileno, LOCK_EX); //lock file.
    if(pFile==NULL){
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"open nginx.conf failed.");
    }
    char tmp[500];
    while(fgets(tmp,500,pFile)!=NULL){
        fputs(tmp,pf);
        if (strstr(tmp,upstreamd)!=NULL){
            fputs(serverd2,pf);
        }
    }
    fclose(pFile);
    fclose(pf);

    //strcat(msg,(char*)(op->server.data));
    strcat(msg,(char*)(op->upstream.data));
    n = ngx_write_fd(fd, msg, 1997);

    if (n == -1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "ngx_write_fd error!");
    }

    if ( ngx_close_file(fd) == NGX_FILE_ERROR ) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                  "ngx_close_file feiled.");
    }
     
    char cmd2[200]="mv -f ";
    strcat(cmd2,tmp_file);
    strcat(cmd2," ");
    strcat(cmd2,cfg_file);
    system(cmd2);
    flock(pf->_fileno, LOCK_UN); //unlock file.
    // ----------------------------------add end-----------

    peers = uscf->peer.data;
    for (peer = peers->peer, last = peer; peer; peer = peer->next) {
        if (op->server.len == peer->name.len && ngx_strncmp(op->server.data, peer->name.data, peer->name.len) == 0) {
            op->status = NGX_HTTP_BAD_REQUEST;
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "server %V already exists in upstream. %s:%d",
                          &op->server,
                          __FUNCTION__,
                          __LINE__);
            return NGX_ERROR;
        }
        last = peer;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url.data = ngx_slab_alloc_locked(shpool, op->server.len);
    if (u.url.data == NULL) {
        op->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory from slab %s:%d",
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }
    ngx_cpystrn(u.url.data, op->server.data, op->server.len + 1);
    u.url.len      = op->server.len;
    u.default_port = 80;

    if (ngx_parse_url_slab(shpool, &u) != NGX_OK) {
        if (u.err) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "%s in upstream \"%V\"", u.err, &u.url);
        }

        op->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return NGX_ERROR;
    }

    last->next = ngx_slab_calloc_locked(shpool, sizeof(ngx_http_upstream_rr_peer_t));
    if (last->next == NULL) {
        op->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory from slab %s:%d",
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }

    last->next->name     = u.url;
    last->next->server   = u.url;
    last->next->sockaddr = u.addrs[0].sockaddr;
    last->next->socklen  = u.addrs[0].socklen;

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_WEIGHT) {
        last->next->weight = op->weight;
        last->next->effective_weight = op->weight;
        last->next->current_weight = 0;
    } else {
        last->next->weight = 1;
        last->next->effective_weight = 1;
        last->next->current_weight = 0;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_MAX_FAILS) {
        last->next->max_fails = op->max_fails;
    } else {
        last->next->max_fails = 1;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_FAIL_TIMEOUT) {
        last->next->fail_timeout = op->fail_timeout;
    } else {
        last->next->fail_timeout = 10;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_DOWN) {
        last->next->down = op->down;
    }

    peers->number++;
    peers->total_weight += last->next->weight;
    peers->single = (peers->number == 1);
    peers->weighted = (peers->total_weight != peers->number);

    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                  "added server %V", &op->server);

    return NGX_OK;
}


static ngx_int_t
ngx_dynamic_upstream_op_remove(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                               ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_http_upstream_rr_peer_t   *peer, *target, *prev;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_uint_t                     weight;
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"rrrrrrrrrrrrmmmmmmmmmmmmmmvvvvvvvvvvvv");
    char * cfg_file = (char *)(uscf->file_name);

    char server_data[200];
    strncpy(server_data,(char*)(op->server.data),25);
    int index=strloc(server_data,"HTTP");
    char serverd[50];
    char serverd2[80];
    strncpy(serverd,(char*)(op->server.data),index-2);
    strcpy(serverd2,"        server ");
    strcat(serverd2,serverd);
    strcat(serverd2,"\n");
    strncpy(server_data,(char*)(op->upstream.data),25);
    index=strloc(server_data,"&remove");
    char upstreamd[50];
    strncpy(upstreamd,(char*)(op->upstream.data),index-1);
    //ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:upstreamd %s %s",serverd,upstreamd);
    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:server_data %s|%s|%s|%s9999999999999",serverd,upstreamd,op->server.data,op->upstream.data);

    FILE * pFile;
    FILE * pf;
    char * tmp_file="//tmp//nginx.conf";
    pFile=fopen(cfg_file,"r");
    pf=fopen(tmp_file,"w");
    flock(pf->_fileno, LOCK_EX);
    if(pFile==NULL){
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"open nginx.conf failed.");
    }
    char tmp[500];
    while(fgets(tmp,500,pFile)!=NULL){
        if (strstr(tmp,serverd)==NULL){
            fputs(tmp,pf);
        }
    }
    fclose(pFile);
    fclose(pf);
  
    char cmd2[200]="mv -f ";
    strcat(cmd2,tmp_file);
    strcat(cmd2," ");
    strcat(cmd2,cfg_file);
    system(cmd2);
    flock(pf->_fileno, LOCK_UN);
//**/
// -------------------------------add end ----------------------------

    peers = uscf->peer.data;

    if (peers->number < 2) {
        op->status = NGX_HTTP_BAD_REQUEST;
        return NGX_ERROR;
    }

    target = NULL;
    prev = NULL;
    for (peer = peers->peer; peer ; peer = peer->next) {
        if (op->server.len == peer->name.len && ngx_strncmp(op->server.data, peer->name.data, peer->name.len) == 0) {
            target = peer;
            peer = peer->next;
            break;
        }
        prev = peer;
    }

    /* not found */
    if (target == NULL) {
        op->status = NGX_HTTP_BAD_REQUEST;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "server %V is not found. %s:%d",
                      &op->server,
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }
    weight = target->weight;
    /* released removed peer and attributes */
    if (ngx_dynamic_upstream_is_shpool_range(r, shpool, target->name.data)) {
        ngx_slab_free_locked(shpool, target->name.data);
    }

    if (ngx_dynamic_upstream_is_shpool_range(r, shpool, target->sockaddr)) {
        ngx_slab_free_locked(shpool, target->sockaddr);
    }

    ngx_slab_free_locked(shpool, target);

    /* found head */
    if (prev == NULL) {
        peers->peer = peer;
        goto ok;
    }

    /* found tail */
    if (peer == NULL) {
        prev->next = NULL;
        goto ok;
    }

    /* found inside */
    prev->next = peer;

 ok:
    peers->number--;
    peers->total_weight -= weight;
    peers->single = (peers->number == 1);
    peers->weighted = (peers->total_weight != peers->number);

    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                  "removed server %V", &op->server);

    return NGX_OK;
}


static ngx_int_t
ngx_dynamic_upstream_op_update_param(ngx_http_request_t *r, ngx_dynamic_upstream_op_t *op,
                                     ngx_slab_pool_t *shpool, ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_http_upstream_rr_peer_t   *peer, *target;
    ngx_http_upstream_rr_peers_t  *peers;

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"vvvvvvvvvvvvvvvvvvvvvVVVVVVV");
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"sssssssss:%s", (u_char *)(uscf->file_name));
    char * cfg_file = (char *)(uscf->file_name);

    char server_data[200];
    strncpy(server_data,(char*)(op->server.data),25);

    peers = uscf->peer.data;

    target = NULL;
    for (peer = peers->peer; peer ; peer = peer->next) {
        if (op->server.len == peer->name.len && ngx_strncmp(op->server.data, peer->name.data, peer->name.len) == 0) {
            target = peer;
            break;
        }
    }

    if (target == NULL) {
        op->status = NGX_HTTP_BAD_REQUEST;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "server %V is not found. %s:%d",
                      &op->server,
                      __FUNCTION__,
                      __LINE__);
        return NGX_ERROR;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_WEIGHT) {
        target->weight = op->weight;
        target->current_weight = op->weight;
        target->effective_weight = op->weight;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_MAX_FAILS) {
        target->max_fails = op->max_fails;
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_FAIL_TIMEOUT) {
        target->fail_timeout = op->fail_timeout;
    }
    
    char serverd[50];
    char serverd2[80];

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_UP) {
        target->down = 0;
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                      "downed server %V", &op->server);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"downed server %V",&op->server);
        //strcat(serverd2," up\n");
        int index=strloc(server_data,"&up");
        strncpy(serverd,(char*)(op->server.data),index-1);
        strcpy(serverd2,"        server ");
        strcat(serverd2,serverd);
        strcat(serverd2,";\n");
        strncpy(server_data,(char*)(op->upstream.data),25);
        index=strloc(server_data,"&server");
        char upstreamd[50];
        strncpy(upstreamd,(char*)(op->upstream.data),index-1);

        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:server_data %s|%s|%s|%s9999999999999",serverd,upstreamd,op->server.data,op->upstream.data);
    }

    if (op->op_param & NGX_DYNAMIC_UPSTEAM_OP_PARAM_DOWN) {
        target->down = 1;
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                      "upped server %V", &op->server);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"upped server %V",&op->server);
        //strcat(serverd2," down\n");
        int index=strloc(server_data,"&down");
        strncpy(serverd,(char*)(op->server.data),index-1);
        strcpy(serverd2,"        server ");
        strcat(serverd2,serverd);
        strcat(serverd2," down;\n");
        strncpy(server_data,(char*)(op->upstream.data),25);
        index=strloc(server_data,"&server");
        char upstreamd[50];
        strncpy(upstreamd,(char*)(op->upstream.data),index-1);

        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"serverd:server_data %s|%s|%s|%s9999999999999",serverd,upstreamd,op->server.data,op->upstream.data);
    }
 
    FILE * pFile;
    FILE * pf;
    char * tmp_file="//tmp//nginx.conf";
    //pFile=fopen("//usr//local//nginx//conf//nginx.conf","r");
    pFile=fopen(cfg_file,"r");
    pf=fopen(tmp_file,"w");
    flock(pf->_fileno, LOCK_EX);
    if(pFile==NULL){
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,"open nginx.conf failed.");
    }
    char tmp[500];
    while(fgets(tmp,500,pFile)!=NULL){
        if (strstr(tmp,serverd)!=NULL){
            fputs(serverd2,pf);
        }else{
            fputs(tmp,pf);
        }
    }
    fclose(pFile);
    fclose(pf);    

    /**char cmd[100]="rm -f ";
    strcat(cmd,cfg_file);
    system(cmd);**/
    char cmd2[200]="mv -f ";
    strcat(cmd2,tmp_file);
    strcat(cmd2," ");
    strcat(cmd2,cfg_file);
    system(cmd2);
    flock(pf->_fileno, LOCK_UN);
    //system("mv //usr//local//nginx//conf//nginx2.conf //usr//local//nginx//conf//nginx.conf");

    return NGX_OK;
}

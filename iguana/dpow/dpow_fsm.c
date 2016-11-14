/******************************************************************************
 * Copyright © 2014-2016 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

struct dpow_entry *dpow_notaryfind(struct supernet_info *myinfo,struct dpow_block *bp,int32_t height,int32_t *senderindp,uint8_t *senderpub)
{
    int32_t i;
    *senderindp = -1;
    for (i=0; i<bp->numnotaries; i++)
    {
        if ( memcmp(bp->notaries[i].pubkey,senderpub,33) == 0 )
        {
            //printf("matches notary.%d\n",i);
            *senderindp = i;
            return(&bp->notaries[i]);
        }
    }
    return(0);
}

void dpow_utxo2entry(struct dpow_block *bp,struct dpow_entry *ep,struct dpow_utxoentry *up)
{
    int32_t i;
    for (i=0; i<bp->numnotaries; i++)
        bp->notaries[i].othermask |= up->othermasks[i];
    ep->commit = up->commit;
    ep->height = up->height;
    ep->recvmask = up->recvmask;
    ep->bestk = up->bestk;
    ep->src.prev_hash = up->srchash;
    ep->dest.prev_hash = up->desthash;
    ep->src.prev_vout = up->srcvout;
    ep->dest.prev_vout = up->destvout;
}

void dpow_entry2utxo(struct dpow_utxoentry *up,struct dpow_block *bp,struct dpow_entry *ep)
{
    int32_t i;
    up->commit = bp->commit;
    up->hashmsg = bp->hashmsg;
    up->height = bp->height;
    up->recvmask = bp->recvmask;
    up->bestk = bp->bestk;
    for (i=0; i<bp->numnotaries; i++)
        up->othermasks[i] = bp->notaries[i].recvmask;
    for (i=0; i<33; i++)
        up->pubkey[i] = ep->pubkey[i];
    up->commit = ep->commit;
    up->height = ep->height;
    up->recvmask = ep->recvmask;
    up->bestk = ep->bestk;
    up->srchash = ep->src.prev_hash;
    up->desthash = ep->dest.prev_hash;
    up->srcvout = ep->src.prev_vout;
    up->destvout = ep->dest.prev_vout;
}

/*void dpow_utxosync(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_block *bp,uint64_t recvmask,int32_t myind,bits256 srchash)
{
    uint32_t i,j,r; int32_t len; struct dpow_utxoentry U; uint8_t utxodata[sizeof(U)+2];
    if ( (bp->recvmask ^ recvmask) != 0 )
    {
        if ( ((1LL << myind) & recvmask) == 0 )
        {
            i = myind;
            //printf("utxosync bp->%llx != %llx, myind.%d\n",(long long)bp->recvmask,(long long)recvmask,myind);
        }
        else
        {
            r = (rand() % bp->numnotaries);
            for (j=0; j<bp->numnotaries; j++)
            {
                i = DPOW_MODIND(bp,j+r);
                if ( ((1LL << i) & bp->recvmask) != 0 && ((1LL << i) & recvmask) == 0 )
                    break;
            }
            //printf("utxosync bp->%llx != %llx, random pick.%d\n",(long long)bp->recvmask,(long long)recvmask,i);
        }
        memset(&U,0,sizeof(U));
        dpow_entry2utxo(&U,bp,&bp->notaries[i]);
        //char str[65],str2[65];
        //printf("send.(%s %s)\n",bits256_str(str,bp->notaries[i].dest.prev_hash),bits256_str(str2,bp->notaries[i].src.prev_hash));
        if ( (len= dpow_rwutxobuf(1,utxodata,&U,bp)) > 0 )
            dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,DPOW_UTXOCHANNEL,bp->height,utxodata,len);
    }
}*/

int32_t dpow_datahandler(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_block *bp,uint8_t nn_senderind,uint32_t channel,uint32_t height,uint8_t *data,int32_t datalen)
{
    int32_t i,src_or_dest,myind = -1; bits256 txid,srchash; struct iguana_info *coin; char str[65],str2[65];
    dpow_notaryfind(myinfo,bp,height,&myind,dp->minerkey33);
    if ( myind < 0 )
    {
        printf("couldnt find myind height.%d | this means your pubkey for this node is not registered and needs to be ratified by majority vote of all notaries\n",height);
        return(-1);
    }
    for (i=0; i<32; i++)
        srchash.bytes[i] = dp->minerkey33[i+1];
    if ( channel == DPOW_TXIDCHANNEL || channel == DPOW_BTCTXIDCHANNEL )
    {
        src_or_dest = (channel == DPOW_BTCTXIDCHANNEL);
        coin = (src_or_dest != 0) ? bp->destcoin : bp->srccoin;
        //printf("bp.%p datalen.%d\n",bp,datalen);
        for (i=0; i<32; i++)
            srchash.bytes[i] = data[i];
        txid = bits256_doublesha256(0,&data[32],datalen-32);
        init_hexbytes_noT(bp->signedtx,&data[32],datalen-32);
        if ( bits256_cmp(txid,srchash) == 0 )
        {
            //printf("verify (%s) it is properly signed! set ht.%d signedtxid to %s\n",coin->symbol,height,bits256_str(str,txid));
            if ( src_or_dest != 0 )
            {
                if ( bp->state < 1000 )
                {
                    bp->desttxid = txid;
                    bp->state = 1000;
                    dp->destupdated = 0;
                    dpow_signedtxgen(myinfo,dp,bp->srccoin,bp,bp->bestk,bp->bestmask,myind,DPOW_SIGCHANNEL,0,bp->isratify);
                    //dpow_sigscheck(myinfo,dp,bp,DPOW_SIGCHANNEL,myind,0);
                }
            }
            else
            {
                if ( bp->state != 0xffffffff )
                {
                    bp->srctxid = txid;
                    printf("set state elapsed %d COMPLETED %s.(%s) %s.(%s)\n",(int32_t)(time(NULL) - bp->starttime),dp->symbol,bits256_str(str,bp->desttxid),dp->dest,bits256_str(str2,txid));
                    bp->state = 0xffffffff;
                }
            }
        }
        else
        {
            init_hexbytes_noT(bp->signedtx,data,datalen);
            printf("txidchannel txid %s mismatch %s (%s)\n",bits256_str(str,txid),bits256_str(str2,srchash),bp->signedtx);
            bp->signedtx[0] = 0;
        }
    } //else printf("unhandled channel.%x\n",channel);
    return(0);
}

int32_t dpow_checkutxo(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_block *bp,struct iguana_info *coin,bits256 *txidp,int32_t *voutp,char *coinaddr)
{
    int32_t haveutxo,completed,minutxo,n; bits256 signedtxid; cJSON *addresses; char *rawtx,*sendtx;
    if ( strcmp("BTC",coin->symbol) == 0 )
    {
        minutxo = 9;
        n = 50;
    }
    else
    {
        minutxo = 49;
        n = 10;
    }
    if ( (haveutxo= dpow_haveutxo(myinfo,coin,txidp,voutp,coinaddr)) <= minutxo && time(NULL) > dp->lastsplit+bp->duration )
    {
        addresses = cJSON_CreateArray();
        jaddistr(addresses,coinaddr);
        if ( (rawtx= iguana_utxoduplicates(myinfo,coin,dp->minerkey33,DPOW_UTXOSIZE,n,&completed,&signedtxid,0,addresses)) != 0 )
        {
            if ( (sendtx= dpow_sendrawtransaction(myinfo,coin,rawtx)) != 0 )
            {
                printf("sendrawtransaction.(%s)\n",sendtx);
                free(sendtx);
            }
            free(rawtx);
        }
        free_json(addresses);
        dp->lastsplit = (uint32_t)time(NULL);
    }
    if ( bits256_nonz(*txidp) == 0 )
        return(-1);
    return(haveutxo);
}

void dpow_statemachinestart(void *ptr)
{
    void **ptrs = ptr;
    struct supernet_info *myinfo; struct dpow_info *dp; struct dpow_checkpoint checkpoint;
    int32_t i,destprevvout0,srcprevvout0,numratified=0,kmdheight,myind = -1; uint8_t pubkeys[64][33]; cJSON *ratified=0,*item; struct iguana_info *src,*dest; char *jsonstr,*handle,*hexstr,str[65],str2[65],srcaddr[64],destaddr[64]; bits256 zero,srchash,destprevtxid0,srcprevtxid0; struct dpow_block *bp; struct dpow_entry *ep = 0; uint32_t duration,minsigs,starttime;
    memset(&zero,0,sizeof(zero));
    srcprevtxid0 = destprevtxid0 = zero;
    srcprevvout0 = destprevvout0 = -1;
    myinfo = ptrs[0];
    dp = ptrs[1];
    minsigs = (uint32_t)(long)ptrs[2];
    duration = (uint32_t)(long)ptrs[3];
    jsonstr = ptrs[4];
    kmdheight = -1;
    memcpy(&checkpoint,&ptrs[5],sizeof(checkpoint));
    printf("statemachinestart %s->%s %s ht.%d minsigs.%d duration.%d start.%u\n",dp->symbol,dp->dest,bits256_str(str,checkpoint.blockhash.hash),checkpoint.blockhash.height,minsigs,duration,checkpoint.timestamp);
    src = iguana_coinfind(dp->symbol);
    dest = iguana_coinfind(dp->dest);
    if ( src == 0 || dest == 0 )
    {
        printf("null coin ptr? (%s %p or %s %p)\n",dp->symbol,src,dp->dest,dest);
        return;
    }
    if ( strcmp(src->symbol,"KMD") == 0 )
        kmdheight = checkpoint.blockhash.height;
    else if ( strcmp(dest->symbol,"KMD") == 0 )
        kmdheight = dest->longestchain;
    if ( (bp= dp->blocks[checkpoint.blockhash.height]) == 0 )
    {
        bp = calloc(1,sizeof(*bp));
        bp->minsigs = minsigs;
        bp->duration = duration;
        bp->srccoin = src;
        bp->destcoin = dest;
        bp->opret_symbol = dp->symbol;
        if ( jsonstr != 0 && (ratified= cJSON_Parse(jsonstr)) != 0 )
        {
            bp->isratify = 1;
            if ( (numratified= cJSON_GetArraySize(ratified)) > 0 )
            {
                for (i=0; i<numratified; i++)
                {
                    item = jitem(ratified,i);
                    hexstr = handle = 0;
                    if ( (hexstr= jstr(item,"pubkey")) != 0 && is_hexstr(hexstr,0) == 66 )
                    {
                        decode_hex(bp->ratified_pubkeys[i],33,hexstr);
                        if ( (handle= jstr(item,"handle")) != 0 )
                            safecopy(bp->handles[i],handle,sizeof(bp->handles[i]));
                        if ( i == 0 )
                        {
                            destprevtxid0 = jbits256(item,"destprevtxid0");
                            destprevvout0 = jint(item,"destprevvout0");
                            srcprevtxid0 = jbits256(item,"srcprevtxid0");
                            srcprevvout0 = jint(item,"srcprevvout0");
                            bp->require0 = 1;
                        }
                    }
                    else
                    {
                        printf("break loop hexstr.%p handle.%p\n",hexstr,handle);
                        break;
                    }
                }
                if ( i == numratified )
                {
                    bp->numratified = numratified;
                    bp->ratified = ratified;
                    printf("numratified.%d %s\n",numratified,jprint(ratified,0));
                }
                else
                {
                    printf("i.%d numratified.%d\n",i,numratified);
                    free_json(ratified);
                }
            }
        }
        if ( bp->isratify == 0 )
            return;
        bp->bestk = -1;
        dp->blocks[checkpoint.blockhash.height] = bp;
        bp->beacon = rand256(0);
        vcalc_sha256(0,bp->commit.bytes,bp->beacon.bytes,sizeof(bp->beacon));
        /*if ( checkpoint.blockhash.height >= DPOW_FIRSTRATIFY && dp->blocks[checkpoint.blockhash.height - DPOW_FIRSTRATIFY] != 0 )
        {
            printf("purge %s.%d\n",dp->dest,checkpoint.blockhash.height - DPOW_FIRSTRATIFY);
            free(dp->blocks[checkpoint.blockhash.height - DPOW_FIRSTRATIFY]);
            dp->blocks[checkpoint.blockhash.height - DPOW_FIRSTRATIFY] = 0;
        }*/
    }
    bitcoin_address(srcaddr,src->chain->pubtype,dp->minerkey33,33);
    bitcoin_address(destaddr,dest->chain->pubtype,dp->minerkey33,33);
    if ( kmdheight >= 0 )
    {
        bp->numnotaries = komodo_notaries(src->symbol,pubkeys,strcmp("KMD",src->symbol) == 0 ? kmdheight : bp->height);
        for (i=0; i<bp->numnotaries; i++)
        {
            //int32_t j; for (j=0; j<33; j++)
            //    printf("%02x",pubkeys[i][j]);
            //printf(" <= pubkey[%d]\n",i);
            memcpy(bp->notaries[i].pubkey,pubkeys[i],33);
            if ( memcmp(bp->notaries[i].pubkey,dp->minerkey33,33) == 0 )
            {
                myind = i;
                ep = &bp->notaries[myind];
            }
        }
        if ( myind < 0 || ep == 0 )
        {
            printf("minerkey33-> ");
            for (i=0; i<33; i++)
                printf("%02x",dp->minerkey33[i]);
            printf(" statemachinestart this node %s %s is not official notary numnotaries.%d\n",srcaddr,destaddr,bp->numnotaries);
            free(ptr);
            return;
        }
    }
    else
    {
        printf("statemachinestart no kmdheight.%d\n",kmdheight);
        free(ptr);
        return;
    }
    if ( bp->isratify != 0 && memcmp(bp->notaries[0].pubkey,bp->ratified_pubkeys[0],33) != 0 )
    {
        for (i=0; i<33; i++)
            printf("%02x",bp->notaries[0].pubkey[i]);
        printf(" current vs ");
        for (i=0; i<33; i++)
            printf("%02x",bp->ratified_pubkeys[0][i]);
        printf(" new, cant change notary0\n");
        return;
    }
    printf(" myind.%d myaddr.(%s %s)\n",myind,srcaddr,destaddr);
    if ( myind == 0 && bits256_nonz(destprevtxid0) != 0 && bits256_nonz(srcprevtxid0) != 0 && destprevvout0 >= 0 && srcprevvout0 >= 0 )
    {
        ep->dest.prev_hash = destprevtxid0;
        ep->dest.prev_vout = destprevvout0;
        ep->src.prev_hash = srcprevtxid0;
        ep->src.prev_vout = srcprevvout0;
        bp->notaries[myind].ratifysrcutxo = srcprevtxid0;
        bp->notaries[myind].ratifysrcvout = srcprevvout0;
        bp->notaries[myind].ratifydestutxo = destprevtxid0;
        bp->notaries[myind].ratifydestvout = destprevvout0;
        printf("Use override utxo %s/v%d %s/v%d\n",bits256_str(str,destprevtxid0),destprevvout0,bits256_str(str2,srcprevtxid0),srcprevvout0);
    }
    else
    {
        if ( dpow_checkutxo(myinfo,dp,bp,bp->destcoin,&ep->dest.prev_hash,&ep->dest.prev_vout,destaddr) < 0 )
        {
            printf("dont have %s %s utxo, please send funds\n",dp->dest,destaddr);
            free(ptr);
            return;
        }
        if ( dpow_checkutxo(myinfo,dp,bp,bp->srccoin,&ep->src.prev_hash,&ep->src.prev_vout,srcaddr) < 0 )
        {
            printf("dont have %s %s utxo, please send funds\n",dp->symbol,srcaddr);
            free(ptr);
            return;
        }
        if ( bp->isratify != 0 )
        {
            bp->notaries[myind].ratifysrcutxo = ep->src.prev_hash;
            bp->notaries[myind].ratifysrcvout = ep->src.prev_vout;
            bp->notaries[myind].ratifydestutxo = ep->dest.prev_hash;
            bp->notaries[myind].ratifydestvout = ep->dest.prev_vout;
        }
    }
    bp->recvmask |= (1LL << myind);
    bp->notaries[myind].othermask |= (1LL << myind);
    dp->checkpoint = checkpoint;
    bp->height = checkpoint.blockhash.height;
    bp->timestamp = checkpoint.timestamp;
    bp->hashmsg = checkpoint.blockhash.hash;
    bp->myind = myind;
    while ( bp->isratify == 0 && dp->destupdated == 0 )
    {
        if ( dp->checkpoint.blockhash.height > checkpoint.blockhash.height )
        {
            printf("abort ht.%d due to new checkpoint.%d\n",checkpoint.blockhash.height,dp->checkpoint.blockhash.height);
            return;
        }
        sleep(1);
    }
    if ( bp->isratify == 0 || (starttime= checkpoint.timestamp) == 0 )
        bp->starttime = starttime = (uint32_t)time(NULL);
    printf("isratify.%d DPOW.%s statemachine checkpoint.%d %s start.%u\n",bp->isratify,src->symbol,checkpoint.blockhash.height,bits256_str(str,checkpoint.blockhash.hash),checkpoint.timestamp);
    for (i=0; i<sizeof(srchash); i++)
        srchash.bytes[i] = dp->minerkey33[i+1];
    //printf("start utxosync start.%u %u\n",starttime,(uint32_t)time(NULL));
    //dpow_utxosync(myinfo,dp,bp,0,myind,srchash);
    //printf("done utxosync start.%u %u\n",starttime,(uint32_t)time(NULL));
    while ( time(NULL) < starttime+bp->duration && src != 0 && dest != 0 && bp->state != 0xffffffff )
    {
        sleep(1);
        if ( dp->checkpoint.blockhash.height > checkpoint.blockhash.height )
        {
            if ( bp->isratify == 0 )
            {
                printf("abort ht.%d due to new checkpoint.%d\n",checkpoint.blockhash.height,dp->checkpoint.blockhash.height);
                break;
            }
            else
            {
                bp->bestk = -1;
                bp->bestmask = 0;
                bp->height = ((dp->checkpoint.blockhash.height / 10) % (DPOW_FIRSTRATIFY/10)) * 10;
                printf("new rotation ht.%d\n",bp->height);
                dp->blocks[checkpoint.blockhash.height] = 0;
                checkpoint.blockhash.height = dp->checkpoint.blockhash.height;
                dp->blocks[checkpoint.blockhash.height] = bp;
            }
        }
        if ( bp->state != 0xffffffff )
        {
            int32_t len; struct dpow_utxoentry U; uint8_t utxodata[sizeof(U)+2];
            memset(&U,0,sizeof(U));
            dpow_entry2utxo(&U,bp,&bp->notaries[myind]);
            if ( (len= dpow_rwutxobuf(1,utxodata,&U,bp)) > 0 )
                dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,DPOW_UTXOCHANNEL,bp->height,utxodata,len);
            else
            {
                dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,0,bp->height,utxodata,0);
                printf("error sending utxobuf\n");
            }
        }
        if ( 0 && dp->cancelratify != 0 && bp->isratify != 0 )
        {
            printf("abort pending ratify\n");
            break;
        }
    }
    printf("bestk.%d %llx sigs.%llx state machine ht.%d completed state.%x %s.%s %s.%s recvmask.%llx\n",bp->bestk,(long long)bp->bestmask,(long long)(bp->bestk>=0?bp->destsigsmasks[bp->bestk]:0),bp->height,bp->state,dp->dest,bits256_str(str,bp->desttxid),dp->symbol,bits256_str(str2,bp->srctxid),(long long)bp->recvmask);
    dp->lastrecvmask = bp->recvmask;
    free(ptr);
}


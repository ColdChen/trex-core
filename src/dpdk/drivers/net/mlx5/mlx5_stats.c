/*-
 *   BSD LICENSE
 *
 *   Copyright 2015 6WIND S.A.
 *   Copyright 2015 Mellanox.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of 6WIND S.A. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/sockios.h>
#include <linux/ethtool.h>

#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_malloc.h>

#include "mlx5.h"
#include "mlx5_rxtx.h"
#include "mlx5_defs.h"

struct mlx5_counter_ctrl {
	/* Name of the counter. */
	char dpdk_name[RTE_ETH_XSTATS_NAME_SIZE];
	/* Name of the counter on the device table. */
	char ctr_name[RTE_ETH_XSTATS_NAME_SIZE];
};

static const struct mlx5_counter_ctrl mlx5_counters_init[] = {
	{
		.dpdk_name = "rx_port_unicast_bytes",
		.ctr_name = "rx_vport_unicast_bytes",
	},
	{
		.dpdk_name = "rx_port_multicast_bytes",
		.ctr_name = "rx_vport_multicast_bytes",
	},
	{
		.dpdk_name = "rx_port_broadcast_bytes",
		.ctr_name = "rx_vport_broadcast_bytes",
	},
	{
		.dpdk_name = "rx_port_unicast_packets",
		.ctr_name = "rx_vport_unicast_packets",
	},
	{
		.dpdk_name = "rx_port_multicast_packets",
		.ctr_name = "rx_vport_multicast_packets",
	},
	{
		.dpdk_name = "rx_port_broadcast_packets",
		.ctr_name = "rx_vport_broadcast_packets",
	},
	{
		.dpdk_name = "tx_port_unicast_bytes",
		.ctr_name = "tx_vport_unicast_bytes",
	},
	{
		.dpdk_name = "tx_port_multicast_bytes",
		.ctr_name = "tx_vport_multicast_bytes",
	},
	{
		.dpdk_name = "tx_port_broadcast_bytes",
		.ctr_name = "tx_vport_broadcast_bytes",
	},
	{
		.dpdk_name = "tx_port_unicast_packets",
		.ctr_name = "tx_vport_unicast_packets",
	},
	{
		.dpdk_name = "tx_port_multicast_packets",
		.ctr_name = "tx_vport_multicast_packets",
	},
	{
		.dpdk_name = "tx_port_broadcast_packets",
		.ctr_name = "tx_vport_broadcast_packets",
	},
	{
		.dpdk_name = "rx_wqe_err",
		.ctr_name = "rx_wqe_err",
	},
	{
		.dpdk_name = "rx_crc_errors_phy",
		.ctr_name = "rx_crc_errors_phy",
	},
	{
		.dpdk_name = "rx_in_range_len_errors_phy",
		.ctr_name = "rx_in_range_len_errors_phy",
	},
	{
		.dpdk_name = "rx_symbol_err_phy",
		.ctr_name = "rx_symbol_err_phy",
	},
	{
		.dpdk_name = "tx_errors_phy",
		.ctr_name = "tx_errors_phy",
	},
	{
		.dpdk_name = "rx_out_of_buffer",
		.ctr_name = "out_of_buffer",
	},
};

static const unsigned int xstats_n = RTE_DIM(mlx5_counters_init);

/**
 * Read device counters table.
 *
 * @param priv
 *   Pointer to private structure.
 * @param[out] stats
 *   Counters table output buffer.
 *
 * @return
 *   0 on success and stats is filled, negative on error.
 */
static int
priv_read_dev_counters(struct priv *priv, uint64_t *stats)
{
	struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
	unsigned int i;
	struct ifreq ifr;
	unsigned int stats_sz = (xstats_ctrl->stats_n * sizeof(uint64_t)) +
				 sizeof(struct ethtool_stats);
	struct ethtool_stats et_stats[(stats_sz + (
				      sizeof(struct ethtool_stats) - 1)) /
				      sizeof(struct ethtool_stats)];

	et_stats->cmd = ETHTOOL_GSTATS;
	et_stats->n_stats = xstats_ctrl->stats_n;
	ifr.ifr_data = (caddr_t)et_stats;
	if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) {
		WARN("unable to read statistic values from device");
		return -1;
	}
	for (i = 0; i != xstats_n; ++i) {
		if (priv_is_ib_cntr(mlx5_counters_init[i].ctr_name))
			priv_get_cntr_sysfs(priv,
					    mlx5_counters_init[i].ctr_name,
					    &stats[i]);
		else
			stats[i] = (uint64_t)
				et_stats->data[xstats_ctrl->dev_table_idx[i]];
	}
	return 0;
}

/**
 * Query the number of statistics provided by ETHTOOL.
 *
 * @param priv
 *   Pointer to private structure.
 *
 * @return
 *   Number of statistics on success, -1 on error.
 */
static int
priv_ethtool_get_stats_n(struct priv *priv) {
	struct ethtool_drvinfo drvinfo;
	struct ifreq ifr;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr.ifr_data = (caddr_t)&drvinfo;
	if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) {
		WARN("unable to query number of statistics");
		return -1;
	}
	return drvinfo.n_stats;
}

/**
 * Init the structures to read device counters.
 *
 * @param priv
 *   Pointer to private structure.
 */
void
priv_xstats_init(struct priv *priv)
{
	struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
	unsigned int i;
	unsigned int j;
	struct ifreq ifr;
	struct ethtool_gstrings *strings = NULL;
	unsigned int dev_stats_n;
	unsigned int str_sz;

	dev_stats_n = priv_ethtool_get_stats_n(priv);
	if (dev_stats_n < 1) {
		WARN("no extended statistics available");
		return;
	}
	xstats_ctrl->stats_n = dev_stats_n;
	/* Allocate memory to grab stat names and values. */
	str_sz = dev_stats_n * ETH_GSTRING_LEN;
	strings = (struct ethtool_gstrings *)
		  rte_malloc("xstats_strings",
			     str_sz + sizeof(struct ethtool_gstrings), 0);
	if (!strings) {
		WARN("unable to allocate memory for xstats");
		return;
	}
	strings->cmd = ETHTOOL_GSTRINGS;
	strings->string_set = ETH_SS_STATS;
	strings->len = dev_stats_n;
	ifr.ifr_data = (caddr_t)strings;
	if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) {
		WARN("unable to get statistic names");
		goto free;
	}
	for (j = 0; j != xstats_n; ++j)
		xstats_ctrl->dev_table_idx[j] = dev_stats_n;
	for (i = 0; i != dev_stats_n; ++i) {
		const char *curr_string = (const char *)
			&strings->data[i * ETH_GSTRING_LEN];

		for (j = 0; j != xstats_n; ++j) {
			if (!strcmp(mlx5_counters_init[j].ctr_name,
				    curr_string)) {
				xstats_ctrl->dev_table_idx[j] = i;
				break;
			}
		}
	}
	for (j = 0; j != xstats_n; ++j) {
		if (priv_is_ib_cntr(mlx5_counters_init[j].ctr_name))
			continue;
		if (xstats_ctrl->dev_table_idx[j] >= dev_stats_n) {
			WARN("counter \"%s\" is not recognized",
			     mlx5_counters_init[j].dpdk_name);
			goto free;
		}
	}
	/* Copy to base at first time. */
	assert(xstats_n <= MLX5_MAX_XSTATS);
	priv_read_dev_counters(priv, xstats_ctrl->base);
free:
	rte_free(strings);
}

/**
 * Get device extended statistics.
 *
 * @param priv
 *   Pointer to private structure.
 * @param[out] stats
 *   Pointer to rte extended stats table.
 *
 * @return
 *   Number of extended stats on success and stats is filled,
 *   negative on error.
 */
static int
priv_xstats_get(struct priv *priv, struct rte_eth_xstat *stats)
{
	struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
	unsigned int i;
	unsigned int n = xstats_n;
	uint64_t counters[n];

	if (priv_read_dev_counters(priv, counters) < 0)
		return -1;
	for (i = 0; i != xstats_n; ++i) {
		stats[i].id = i;
		stats[i].value = (counters[i] - xstats_ctrl->base[i]);
	}
	return n;
}

/**
 * Reset device extended statistics.
 *
 * @param priv
 *   Pointer to private structure.
 */
static void
priv_xstats_reset(struct priv *priv)
{
	struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
	unsigned int i;
	unsigned int n = xstats_n;
	uint64_t counters[n];

	if (priv_read_dev_counters(priv, counters) < 0)
		return;
	for (i = 0; i != n; ++i)
		xstats_ctrl->base[i] = counters[i];
}

#ifndef TREX_PATCH
/**
 * DPDK callback to get device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] stats
 *   Stats structure output buffer.
 */
int
mlx5_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
	struct priv *priv = mlx5_get_priv(dev);
	struct rte_eth_stats tmp = {0};
	unsigned int i;
	unsigned int idx;

	priv_lock(priv);
	/* Add software counters. */
	for (i = 0; (i != priv->rxqs_n); ++i) {
		struct mlx5_rxq_data *rxq = (*priv->rxqs)[i];

		if (rxq == NULL)
			continue;
		idx = rxq->stats.idx;
		if (idx < RTE_ETHDEV_QUEUE_STAT_CNTRS) {
#ifdef MLX5_PMD_SOFT_COUNTERS
			tmp.q_ipackets[idx] += rxq->stats.ipackets;
			tmp.q_ibytes[idx] += rxq->stats.ibytes;
#endif
			tmp.q_errors[idx] += (rxq->stats.idropped +
					      rxq->stats.rx_nombuf);
		}
#ifdef MLX5_PMD_SOFT_COUNTERS
		tmp.ipackets += rxq->stats.ipackets;
		tmp.ibytes += rxq->stats.ibytes;
#endif
		tmp.ierrors += rxq->stats.idropped;
		tmp.rx_nombuf += rxq->stats.rx_nombuf;
	}
	for (i = 0; (i != priv->txqs_n); ++i) {
		struct mlx5_txq_data *txq = (*priv->txqs)[i];

		if (txq == NULL)
			continue;
		idx = txq->stats.idx;
		if (idx < RTE_ETHDEV_QUEUE_STAT_CNTRS) {
#ifdef MLX5_PMD_SOFT_COUNTERS
			tmp.q_opackets[idx] += txq->stats.opackets;
			tmp.q_obytes[idx] += txq->stats.obytes;
#endif
			tmp.q_errors[idx] += txq->stats.oerrors;
		}
#ifdef MLX5_PMD_SOFT_COUNTERS
		tmp.opackets += txq->stats.opackets;
		tmp.obytes += txq->stats.obytes;
#endif
		tmp.oerrors += txq->stats.oerrors;
	}
#ifndef MLX5_PMD_SOFT_COUNTERS
	/* FIXME: retrieve and add hardware counters. */
#endif
	*stats = tmp;
	priv_unlock(priv);
	return 0;
}


/**
 * DPDK callback to clear device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_stats_reset(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;
	unsigned int idx;

	priv_lock(priv);
	for (i = 0; (i != priv->rxqs_n); ++i) {
		if ((*priv->rxqs)[i] == NULL)
			continue;
		idx = (*priv->rxqs)[i]->stats.idx;
		(*priv->rxqs)[i]->stats =
			(struct mlx5_rxq_stats){ .idx = idx };
	}
	for (i = 0; (i != priv->txqs_n); ++i) {
		if ((*priv->txqs)[i] == NULL)
			continue;
		idx = (*priv->txqs)[i]->stats.idx;
		(*priv->txqs)[i]->stats =
			(struct mlx5_txq_stats){ .idx = idx };
	}
#ifndef MLX5_PMD_SOFT_COUNTERS
	/* FIXME: reset hardware counters. */
#endif
	priv_unlock(priv);
}

#endif

#ifdef TREX_PATCH

static void
mlx5_stats_read_hw(struct rte_eth_dev *dev,
                   struct rte_eth_stats *stats,
                   int init){
    struct priv *priv = mlx5_get_priv(dev);
    struct mlx5_stats_priv * lps = &priv->m_stats;
    unsigned int i;

    struct rte_eth_stats tmp = {0};
    struct ethtool_stats    *et_stats   = (struct ethtool_stats    *)lps->et_stats;
    struct ifreq ifr;

    et_stats->cmd = ETHTOOL_GSTATS;
    et_stats->n_stats = lps->n_stats;

    ifr.ifr_data = (caddr_t) et_stats;

    if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) { 
        WARN("unable to get statistic values for mlnx5 "); 
    }

    tmp.ibytes += et_stats->data[lps->inx_rx_vport_unicast_bytes] +
                  et_stats->data[lps->inx_rx_vport_multicast_bytes] +
                  et_stats->data[lps->inx_rx_vport_broadcast_bytes];

    if (init) {
        tmp.ipackets += et_stats->data[lps->inx_rx_vport_unicast_packets] +
                    et_stats->data[lps->inx_rx_vport_multicast_packets] +
                    et_stats->data[lps->inx_rx_vport_broadcast_packets];

        /* woprkaround CX-4 Lx does not implement tx_vport_unicast_packets and return always ZERO */
        if ( et_stats->data[lps->inx_tx_vport_unicast_packets]==0 ) {
            lps->cx_4_workaround=1;
        }

        if ( lps->cx_4_workaround == 0 ) {
            tmp.opackets += (et_stats->data[lps->inx_tx_vport_unicast_packets] +
                             et_stats->data[lps->inx_tx_vport_multicast_packets] +
                             et_stats->data[lps->inx_tx_vport_broadcast_packets]);
        }else{
            tmp.opackets += (et_stats->data[lps->inx_tx_packets_phy] );
        }

        lps->m_t_opackets =0;
        lps->m_t_ipackets =0;

        lps->m_old_ipackets = (uint32_t)tmp.ipackets;
        lps->m_old_opackets = (uint32_t)tmp.opackets;


    }else{

        /* diff of 32bit */
        uint32_t ipackets = (uint32_t)(et_stats->data[lps->inx_rx_vport_unicast_packets] +
                                       et_stats->data[lps->inx_rx_vport_multicast_packets] +
                                       et_stats->data[lps->inx_rx_vport_broadcast_packets]);

        lps->m_t_ipackets +=(ipackets - lps->m_old_ipackets);

        tmp.ipackets = lps->m_t_ipackets;

        lps->m_old_ipackets = ipackets;


        uint32_t opackets;
        if ( lps->cx_4_workaround == 0 ) {

        /* diff of 32bit */ 
          opackets = (uint32_t)(et_stats->data[lps->inx_tx_vport_unicast_packets] +
                                       et_stats->data[lps->inx_tx_vport_multicast_packets] +
                                       et_stats->data[lps->inx_tx_vport_broadcast_packets]);
        }else{

          opackets = (uint32_t)(et_stats->data[lps->inx_tx_packets_phy] );

        }

        lps->m_t_opackets +=(opackets - lps->m_old_opackets);
        tmp.opackets = lps->m_t_opackets;
        lps->m_old_opackets =opackets;

    }

    tmp.ierrors += 	(et_stats->data[lps->inx_rx_wqe_err] +
                    et_stats->data[lps->inx_rx_crc_errors_phy] +
                    et_stats->data[lps->inx_rx_in_range_len_errors_phy] +
                    et_stats->data[lps->inx_rx_symbol_err_phy]);

    if ( lps->cx_4_workaround == 0 ) {
        tmp.obytes += et_stats->data[lps->inx_tx_vport_unicast_bytes] +
                      et_stats->data[lps->inx_tx_vport_multicast_bytes] +
                      et_stats->data[lps->inx_tx_vport_broadcast_bytes];
    }else{
        tmp.obytes += (et_stats->data[lps->inx_tx_bytes_phy] - (tmp.opackets*4)) ;
    }


    tmp.oerrors += et_stats->data[lps->inx_tx_errors_phy];

    /* SW Rx */
    for (i = 0; (i != priv->rxqs_n); ++i) {
        struct mlx5_rxq_data *rxq = (*priv->rxqs)[i];
        if (rxq) {
            tmp.imissed += rxq->stats.idropped;
            tmp.rx_nombuf += rxq->stats.rx_nombuf;
        }
    }

    /*SW Tx */
    for (i = 0; (i != priv->txqs_n); ++i) {
        struct mlx5_txq_data *txq = (*priv->txqs)[i];
        if (txq) {
            tmp.oerrors += txq->stats.odropped;
        }
    }

    *stats =tmp;
}

void
mlx5_stats_free(struct rte_eth_dev *dev)
{
    struct priv *priv = mlx5_get_priv(dev);
    struct mlx5_stats_priv * lps = &priv->m_stats;

    if ( lps->et_stats ){
        free(lps->et_stats);
        lps->et_stats=0;
    }
}


static void
mlx5_stats_init(struct rte_eth_dev *dev)
{
    struct priv *priv = mlx5_get_priv(dev);
    struct mlx5_stats_priv * lps = &priv->m_stats;
    struct rte_eth_stats tmp = {0};

    unsigned int i;
    unsigned int idx;
    char ifname[IF_NAMESIZE];
    struct ifreq ifr;

    struct ethtool_stats    *et_stats   = NULL;
    struct ethtool_drvinfo drvinfo;
    struct ethtool_gstrings *strings = NULL;
    unsigned int n_stats, sz_str, sz_stats;

    if (priv_get_ifname(priv, &ifname)) {
            WARN("unable to get interface name");
            return;
    }
    /* How many statistics are available ? */
    drvinfo.cmd = ETHTOOL_GDRVINFO;
    ifr.ifr_data = (caddr_t) &drvinfo;
    if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) {
            WARN("unable to get driver info for %s", ifname);
            return;
    }

    n_stats = drvinfo.n_stats;
    if (n_stats < 1) {
            WARN("no statistics available for %s", ifname);
            return;
    }
    lps->n_stats = n_stats;

    /* Allocate memory to grab stat names and values */ 
    sz_str = n_stats * ETH_GSTRING_LEN; 
    sz_stats = n_stats * sizeof(uint64_t); 
    strings = calloc(1, sz_str + sizeof(struct ethtool_gstrings)); 
    if (!strings) { 
        WARN("unable to allocate memory for strings"); 
        return;
    } 

    et_stats = calloc(1, sz_stats + sizeof(struct ethtool_stats)); 
    if (!et_stats) { 
        free(strings);
        WARN("unable to allocate memory for stats"); 
    } 

    strings->cmd = ETHTOOL_GSTRINGS; 
    strings->string_set = ETH_SS_STATS; 
    strings->len = n_stats; 
    ifr.ifr_data = (caddr_t) strings; 
    if (priv_ifreq(priv, SIOCETHTOOL, &ifr) != 0) { 
        WARN("unable to get statistic names for %s", ifname); 
        free(strings);
        free(et_stats);
        return;
    } 

    for (i = 0; (i != n_stats); ++i) {

            const char * curr_string = (const char*) &(strings->data[i * ETH_GSTRING_LEN]);

            if (!strcmp("rx_vport_unicast_bytes", curr_string)) lps->inx_rx_vport_unicast_bytes = i;
            if (!strcmp("rx_vport_multicast_bytes", curr_string)) lps->inx_rx_vport_multicast_bytes = i;
            if (!strcmp("rx_vport_broadcast_bytes", curr_string)) lps->inx_rx_vport_broadcast_bytes = i;

            if (!strcmp("rx_vport_unicast_packets", curr_string)) lps->inx_rx_vport_unicast_packets = i;
            if (!strcmp("rx_vport_multicast_packets", curr_string)) lps->inx_rx_vport_multicast_packets = i;
            if (!strcmp("rx_vport_broadcast_packets", curr_string)) lps->inx_rx_vport_broadcast_packets = i;

            if (!strcmp("tx_vport_unicast_bytes", curr_string)) lps->inx_tx_vport_unicast_bytes = i;
            if (!strcmp("tx_vport_multicast_bytes", curr_string)) lps->inx_tx_vport_multicast_bytes = i;
            if (!strcmp("tx_vport_broadcast_bytes", curr_string)) lps->inx_tx_vport_broadcast_bytes = i;

            if (!strcmp("tx_vport_unicast_packets", curr_string)) lps->inx_tx_vport_unicast_packets = i;
            if (!strcmp("tx_vport_multicast_packets", curr_string)) lps->inx_tx_vport_multicast_packets = i;
            if (!strcmp("tx_vport_broadcast_packets", curr_string)) lps->inx_tx_vport_broadcast_packets = i;

            if (!strcmp("tx_packets_phy", curr_string)) lps->inx_tx_packets_phy = i;
            if (!strcmp("tx_bytes_phy", curr_string)) lps->inx_tx_bytes_phy = i;
            lps->cx_4_workaround=0;

            if (!strcmp("rx_wqe_err", curr_string)) lps->inx_rx_wqe_err = i;
            if (!strcmp("rx_crc_errors_phy", curr_string)) lps->inx_rx_crc_errors_phy = i;
            if (!strcmp("rx_in_range_len_errors_phy", curr_string)) lps->inx_rx_in_range_len_errors_phy = i;
            if (!strcmp("rx_symbol_err_phy", curr_string)) lps->inx_rx_symbol_err_phy = i;

            if (!strcmp("tx_errors_phy", curr_string)) lps->inx_tx_errors_phy = i;
    }

    lps->et_stats =(void *)et_stats;

    if (!lps->inx_rx_vport_unicast_bytes ||
    !lps->inx_rx_vport_multicast_bytes ||
    !lps->inx_rx_vport_broadcast_bytes || 
    !lps->inx_rx_vport_unicast_packets ||
    !lps->inx_rx_vport_multicast_packets ||
    !lps->inx_rx_vport_broadcast_packets ||
    !lps->inx_tx_vport_unicast_bytes || 
    !lps->inx_tx_vport_multicast_bytes ||
    !lps->inx_tx_vport_broadcast_bytes ||
    !lps->inx_tx_vport_unicast_packets ||
    !lps->inx_tx_vport_multicast_packets ||
    !lps->inx_tx_vport_broadcast_packets ||
    !lps->inx_rx_wqe_err ||
    !lps->inx_rx_crc_errors_phy ||
    !lps->inx_rx_in_range_len_errors_phy||
    !lps->inx_tx_packets_phy||
    !lps->inx_tx_bytes_phy) {
        WARN("Counters are not recognized %s", ifname);
        return;
    }

    mlx5_stats_read_hw(dev,&tmp,1);



    /* copy yo shadow at first time */
    lps->m_shadow = tmp;

    free(strings);
}


static void
mlx5_stats_diff(struct rte_eth_stats *a,
                struct rte_eth_stats *b,
                struct rte_eth_stats *c){
    #define MLX5_DIFF(cnt) { a->cnt = (b->cnt - c->cnt);  }

    MLX5_DIFF(ipackets);
    MLX5_DIFF(opackets); 
    MLX5_DIFF(ibytes); 
    MLX5_DIFF(obytes);
    MLX5_DIFF(imissed);

    MLX5_DIFF(ierrors); 
    MLX5_DIFF(oerrors); 
    MLX5_DIFF(rx_nombuf);
}


int mlx5_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
	struct priv *priv = mlx5_get_priv(dev);

    struct mlx5_stats_priv * lps = &priv->m_stats;
    priv_lock(priv);

    if (lps->et_stats == NULL) {
        mlx5_stats_init(dev);
    }
    struct rte_eth_stats tmp = {0};

    mlx5_stats_read_hw(dev,&tmp,0);

    mlx5_stats_diff(stats,
                    &tmp,
                    &lps->m_shadow);

	priv_unlock(priv);
    return(0);
}

/**
 * DPDK callback to clear device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_stats_reset(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
    struct mlx5_stats_priv * lps = &priv->m_stats;

    priv_lock(priv);

    if (lps->et_stats == NULL) {
        mlx5_stats_init(dev);
    }
    struct rte_eth_stats tmp = {0};


    mlx5_stats_read_hw(dev,&tmp,0);

    /* copy to shadow */
    lps->m_shadow = tmp;

	priv_unlock(priv);
}



#endif

/**
 * DPDK callback to get extended device statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] stats
 *   Stats table output buffer.
 * @param n
 *   The size of the stats table.
 *
 * @return
 *   Number of xstats on success, negative on failure.
 */
int
mlx5_xstats_get(struct rte_eth_dev *dev,
		struct rte_eth_xstat *stats, unsigned int n)
{
	struct priv *priv = mlx5_get_priv(dev);
	int ret = xstats_n;

	if (n >= xstats_n && stats) {
		struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
		int stats_n;

		priv_lock(priv);
		stats_n = priv_ethtool_get_stats_n(priv);
		if (stats_n < 0) {
			priv_unlock(priv);
			return -1;
		}
		if (xstats_ctrl->stats_n != stats_n)
			priv_xstats_init(priv);
		ret = priv_xstats_get(priv, stats);
		priv_unlock(priv);
	}
	return ret;
}

/**
 * DPDK callback to clear device extended statistics.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_xstats_reset(struct rte_eth_dev *dev)
{
	struct priv *priv = mlx5_get_priv(dev);
	struct mlx5_xstats_ctrl *xstats_ctrl = &priv->xstats_ctrl;
	int stats_n;

	priv_lock(priv);
	stats_n = priv_ethtool_get_stats_n(priv);
	if (stats_n < 0)
		goto unlock;
	if (xstats_ctrl->stats_n != stats_n)
		priv_xstats_init(priv);
	priv_xstats_reset(priv);
unlock:
	priv_unlock(priv);
}

/**
 * DPDK callback to retrieve names of extended device statistics
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] xstats_names
 *   Buffer to insert names into.
 * @param n
 *   Number of names.
 *
 * @return
 *   Number of xstats names.
 */
int
mlx5_xstats_get_names(struct rte_eth_dev *dev,
		struct rte_eth_xstat_name *xstats_names, unsigned int n)
{
	struct priv *priv = mlx5_get_priv(dev);
	unsigned int i;

	if (n >= xstats_n && xstats_names) {
		priv_lock(priv);
		for (i = 0; i != xstats_n; ++i) {
			strncpy(xstats_names[i].name,
				mlx5_counters_init[i].dpdk_name,
				RTE_ETH_XSTATS_NAME_SIZE);
			xstats_names[i].name[RTE_ETH_XSTATS_NAME_SIZE - 1] = 0;
		}
		priv_unlock(priv);
	}
	return xstats_n;
}

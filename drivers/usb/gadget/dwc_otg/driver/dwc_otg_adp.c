/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_adp.c $
 * $Revision: #6 $
 * $Date: 2010/03/09 $
 * $Change: 1458980 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

#include "dwc_os.h"
#include "dwc_otg_regs.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_adp.h"

/** @file
 *
 * This file contains the most of the Attach Detect Protocol implementation for
 * the driver to support OTG Rev2.0.
 *
 */

void dwc_otg_adp_write_reg(dwc_otg_core_if_t *core_if, uint32_t value)
{
	adpctl_data_t adpctl;
	unsigned int access_request_count = 10;

	adpctl.d32 = value;
	adpctl.b.ar = 0x2;

	dwc_write_reg32(&core_if->core_global_regs->adpctl, adpctl.d32);

	while (adpctl.b.ar && (access_request_count--)) {
		adpctl.d32 = dwc_read_reg32(&core_if->core_global_regs->adpctl);
		dwc_mdelay(100);
	}

}

/**
 * Function is called to read ADP registers
 */
uint32_t dwc_otg_adp_read_reg(dwc_otg_core_if_t *core_if)
{
	adpctl_data_t adpctl;
	unsigned int access_request_count = 10;

	adpctl.d32 = 0;
	adpctl.b.ar = 0x1;

	dwc_write_reg32(&core_if->core_global_regs->adpctl, adpctl.d32);

	while (adpctl.b.ar && (access_request_count--)) {
		adpctl.d32 = dwc_read_reg32(&core_if->core_global_regs->adpctl);
		dwc_mdelay(100);
	}

	return adpctl.d32;
}

/**
 * Function is called to write ADP registers
 */
void dwc_otg_adp_modify_reg(dwc_otg_core_if_t *core_if, uint32_t clr,
			    uint32_t set)
{
	dwc_otg_adp_write_reg(core_if,
			      (dwc_otg_adp_read_reg(core_if) & (~clr)) | set);
}

static void adp_sense_timeout(void *ptr)
{
	dwc_otg_core_if_t *core_if = (dwc_otg_core_if_t *)ptr;
	core_if->adp.sense_timer_started = 0;

	if (core_if->adp_enable) {
		dwc_otg_adp_sense_stop(core_if);
		dwc_otg_adp_probe_start(core_if);
	}
}

/**
 * This function is called when the SRP timer expires. The SRP should
 * complete within 6 seconds.
 */
static void adp_vbuson_timeout(void *ptr)
{
	gpwrdn_data_t gpwrdn;
	dwc_otg_core_if_t *core_if = (dwc_otg_core_if_t *)ptr;
	hprt0_data_t hprt0 = {.d32 = 0 };
	pcgcctl_data_t pcgcctl = {.d32 = 0 };

	if (core_if) {

		core_if->adp.vbuson_timer_started = 0;

		/* Turn off vbus */
		hprt0.b.prtpwr = 1;
		dwc_modify_reg32(core_if->host_if->hprt0, 0, hprt0.d32);

		gpwrdn.d32 = 0;

		/* Power off the core */
		if (core_if->power_down == 2) {
			/* Enable Wakeup Logic */
/*			gpwrdn.b.wkupactiv = 1; */
			gpwrdn.b.pmuactv = 0;
			gpwrdn.b.pwrdnrstn = 1;
			gpwrdn.b.pwrdnclmp = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
					 gpwrdn.d32);

			/* Suspend the Phy Clock */
			pcgcctl.b.stoppclk = 1;
			dwc_modify_reg32(core_if->pcgcctl, 0, pcgcctl.d32);

			/* Switch on VDD */
/*			gpwrdn.b.wkupactiv = 1; */
			gpwrdn.b.pmuactv = 1;
			gpwrdn.b.pwrdnrstn = 1;
			gpwrdn.b.pwrdnclmp = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
					 gpwrdn.d32);
		} else {
			/* Enable Power Down Logic */
			gpwrdn.b.pmuactv = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
					 gpwrdn.d32);
		}

		/* Power off the core */
		if (core_if->power_down == 2) {
			gpwrdn.d32 = 0;
			gpwrdn.b.pwrdnswtch = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn,
					 gpwrdn.d32, 0);
		}

		/* Unmask SRP detected interrupt from Power Down Logic */
		gpwrdn.d32 = 0;
		gpwrdn.b.srp_det_msk = 1;
		dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);

		dwc_otg_adp_probe_start(core_if);
	}

}

/**
 * Start the ADP Initial Probe timer to detect if Port Connected interrupt is not asserted within
 * 1.1 seconds.
 *
 * @param core_if the pointer to core_if strucure.
 */
void dwc_otg_adp_vbuson_timer_start(dwc_otg_core_if_t *core_if)
{
	core_if->adp.vbuson_timer_started = 1;
	DWC_TIMER_SCHEDULE(core_if->adp.vbuson_timer, 1100); /* 1.1 secs */
}

/**
 * Masks all DWC OTG core interrupts
 *
 */
static void mask_all_interrupts(dwc_otg_core_if_t *core_if)
{
	int i;
	gahbcfg_data_t ahbcfg = {.d32 = 0 };

	/* Mask Host Interrupts */

	/* Clear and disable HCINTs */
	for (i = 0; i < core_if->core_params->host_channels; i++) {
		dwc_write_reg32(&core_if->host_if->hc_regs[i]->hcintmsk, 0);
		dwc_write_reg32(&core_if->host_if->hc_regs[i]->hcint,
				0xFFFFFFFF);

	}

	/* Clear and disable HAINT */
	dwc_write_reg32(&core_if->host_if->host_global_regs->haintmsk, 0x0000);
	dwc_write_reg32(&core_if->host_if->host_global_regs->haint, 0xFFFFFFFF);

	/* Mask Device Interrupts */
	if (!core_if->multiproc_int_enable) {
		/* Clear and disable IN Endpoint interrupts */
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->diepmsk, 0);
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++)
			dwc_write_reg32(&core_if->dev_if->in_ep_regs[i]->
					diepint, 0xFFFFFFFF);

		/* Clear and disable OUT Endpoint interrupts */
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->doepmsk, 0);
		for (i = 0; i <= core_if->dev_if->num_out_eps; i++)
			dwc_write_reg32(&core_if->dev_if->out_ep_regs[i]->
					doepint, 0xFFFFFFFF);

		/* Clear and disable DAINT */
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->daint,
				0xFFFFFFFF);
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->daintmsk, 0);
	} else {
		for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
			dwc_write_reg32(&core_if->dev_if->dev_global_regs->
					diepeachintmsk[i], 0);
			dwc_write_reg32(&core_if->dev_if->in_ep_regs[i]->
					diepint, 0xFFFFFFFF);
		}

		for (i = 0; i < core_if->dev_if->num_out_eps; ++i) {
			dwc_write_reg32(&core_if->dev_if->dev_global_regs->
					doepeachintmsk[i], 0);
			dwc_write_reg32(&core_if->dev_if->out_ep_regs[i]->
					doepint, 0xFFFFFFFF);
		}

		dwc_write_reg32(&core_if->dev_if->dev_global_regs->deachintmsk,
				0);
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->deachint,
				0xFFFFFFFF);

	}

	/* Disable interrupts */
	ahbcfg.b.glblintrmsk = 1;
	dwc_modify_reg32(&core_if->core_global_regs->gahbcfg, ahbcfg.d32, 0);

	/* Disable all interrupts. */
	dwc_write_reg32(&core_if->core_global_regs->gintmsk, 0);

	/* Clear any pending interrupts */
	dwc_write_reg32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* Clear any pending OTG Interrupts */
	dwc_write_reg32(&core_if->core_global_regs->gotgint, 0xFFFFFFFF);
}

#if 0
/**
 * Unmask Port Connection Detected interrupt
 *
 */
static void unmask_conn_det_intr(dwc_otg_core_if_t *core_if)
{
	gintmsk_data_t gintmsk = {
		.d32 = 0, .b.portintr = 1
	};

	dwc_write_reg32(&core_if->core_global_regs->gintmsk, gintmsk.d32);
}
#endif

/**
 * Unmask Session Request interrupt
 *
 */
static void unmask_sess_req_intr(dwc_otg_core_if_t *core_if)
{
	gintmsk_data_t gintmsk = {
		.d32 = 0, .b.sessreqintr = 1
	};

	dwc_write_reg32(&core_if->core_global_regs->gintmsk, gintmsk.d32);
}

static void dwc_otg_wait_for_adp_reset_completion(dwc_otg_core_if_t *core_if)
{
	adpctl_data_t adpctl;
	unsigned int reset_count = 10;

	adpctl.d32 = dwc_otg_adp_read_reg(core_if);

	while (adpctl.b.adpres && (reset_count--)) {
		dwc_mdelay(100);
		adpctl.d32 = dwc_otg_adp_read_reg(core_if);
	}
}

/**
 * Starts the ADP Probing
 *
 * @param core_if the pointer to core_if strucure.
 */
uint32_t dwc_otg_adp_probe_start(dwc_otg_core_if_t *core_if)
{

	adpctl_data_t adpctl;

	dwc_otg_disable_global_interrupts(core_if);
	/*  TODO - check: most probably this is not required */
	mask_all_interrupts(core_if);

	/* TODO - check: most probably this is not required */
	if (dwc_otg_is_host_mode(core_if))
		unmask_sess_req_intr(core_if);

	dwc_otg_enable_global_interrupts(core_if);

	core_if->adp.probe_enabled = 1;

	adpctl.b.adpres = 1;
	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	dwc_otg_wait_for_adp_reset_completion(core_if);

	adpctl.d32 = 0;
	adpctl.b.adp_tmout_int_msk = 1;
	adpctl.b.adp_prb_int_msk = 1;
	adpctl.b.prb_dschg = 1;
	adpctl.b.prb_delta = 1;
	adpctl.b.prb_per = 1;
	adpctl.b.adpen = 1;
	adpctl.b.enaprb = 1;

	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	return 0;
}

/**
 * Starts the ADP Sense timer to detect if ADP Sense interrupt is not asserted within
 * 3 seconds.
 *
 * @param core_if the pointer to core_if strucure.
 */
void dwc_otg_adp_sense_timer_start(dwc_otg_core_if_t *core_if)
{
	core_if->adp.sense_timer_started = 1;
	DWC_TIMER_SCHEDULE(core_if->adp.sense_timer, 3000); /* 3 secs */
}

/**
 * Starts the ADP Sense
 *
 * @param core_if the pointer to core_if strucure.
 */
uint32_t dwc_otg_adp_sense_start(dwc_otg_core_if_t *core_if)
{
	adpctl_data_t adpctl;

	dwc_otg_disable_global_interrupts(core_if);

	core_if->adp.sense_enabled = 1;

	adpctl.b.adpres = 1;
	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	dwc_otg_wait_for_adp_reset_completion(core_if);

	adpctl.b.adpen = 1;
	adpctl.b.enasns = 1;
	adpctl.b.adp_sns_int_msk = 1;

	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	dwc_otg_adp_sense_timer_start(core_if);

	return 0;
}

/**
 * Stops the ADP Probing
 *
 * @param core_if the pointer to core_if strucure.
 */
uint32_t dwc_otg_adp_probe_stop(dwc_otg_core_if_t *core_if)
{

	adpctl_data_t adpctl;

	core_if->adp.probe_enabled = 0;
	adpctl.b.adpres = 1;
	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	dwc_otg_wait_for_adp_reset_completion(core_if);

	dwc_otg_adp_write_reg(core_if, 0);

	dwc_otg_enable_global_interrupts(core_if);

	return 0;
}

/**
 * Stops the ADP Sensing
 *
 * @param core_if the pointer to core_if strucure.
 */
uint32_t dwc_otg_adp_sense_stop(dwc_otg_core_if_t *core_if)
{
	adpctl_data_t adpctl;
	core_if->adp.sense_enabled = 0;

	adpctl.b.adpres = 1;
	dwc_otg_adp_write_reg(core_if, adpctl.d32);

	dwc_otg_wait_for_adp_reset_completion(core_if);

	dwc_otg_adp_write_reg(core_if, 0);

	dwc_otg_enable_global_interrupts(core_if);

	return 0;
}

/**
 * Called right after driver is loaded
 * to perform initial actions for ADP
 *
 * @param core_if the pointer to core_if strucure.
 */
void dwc_otg_adp_turnon_vbus(dwc_otg_core_if_t *core_if)
{
	hprt0_data_t hprt0 = {.d32 = 0 };
	hprt0.b.prtpwr = 1;
	dwc_modify_reg32(core_if->host_if->hprt0, 0, hprt0.d32);

/*	unmask_conn_det_intr(core_if); */

/*	dwc_otg_enable_global_interrupts(core_if); */

	dwc_otg_adp_vbuson_timer_start(core_if);
}

/**
 * Called right after driver is loaded
 * to perform initial actions for ADP
 *
 * @param core_if the pointer to core_if strucure.
 */
void dwc_otg_adp_start(dwc_otg_core_if_t *core_if)
{
	gpwrdn_data_t gpwrdn;
	if (core_if->adp_enable) {
#if 0				/* most possibly should be removed */
		mask_all_interrupts(core_if);
#endif
		/* Enable Power Down Logic */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuactv = 1;
		dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);

		/* Unmask SRP detected interrupt from Power Down Logic */
		gpwrdn.d32 = 0;
		gpwrdn.b.srp_det_msk = 1;
		dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);

		gpwrdn.d32 = dwc_read_reg32(&core_if->core_global_regs->gpwrdn);

		/* check which value is for device mode and which for Host mode */
		if (gpwrdn.b.idsts) {	/* TODO - considered host mode value is 1 */
			core_if->adp.initial_probe = 1;
			dwc_otg_adp_probe_start(core_if);
		} else {
			gotgctl_data_t gotgctl;

			gotgctl.d32 =
			    dwc_read_reg32(&core_if->core_global_regs->gotgctl);

			if (gotgctl.b.bsesvld == 0) {
				core_if->adp.initial_probe = 1;
				dwc_otg_adp_probe_start(core_if);
			} else { /** @todo - check if device initialization should be performed here */
				core_if->op_state = B_PERIPHERAL;
				dwc_otg_core_init(core_if);
				dwc_otg_enable_global_interrupts(core_if);
				cil_pcd_start(core_if);
			}
		}
	}
}

void dwc_otg_adp_init(dwc_otg_core_if_t *core_if)
{
	core_if->adp.initial_probe = 0;
	core_if->adp.probe_timer_values[0] = -1;
	core_if->adp.probe_timer_values[1] = -1;
	core_if->adp.probe_enabled = 0;
	core_if->adp.sense_enabled = 0;
	core_if->adp.sense_timer_started = 0;
	core_if->adp.vbuson_timer_started = 0;
	core_if->adp.attached = DWC_OTG_ADP_UNKOWN;
	/* Initialize timers */
	core_if->adp.sense_timer =
	    DWC_TIMER_ALLOC("ADP SENSE TIMER", adp_sense_timeout, core_if);

	core_if->adp.vbuson_timer =
	    DWC_TIMER_ALLOC("ADP VBUS ON TIMER", adp_vbuson_timeout, core_if);
}

void dwc_otg_adp_remove(dwc_otg_core_if_t *core_if)
{
	DWC_TIMER_FREE(core_if->adp.sense_timer);
	DWC_TIMER_FREE(core_if->adp.vbuson_timer);
}

/*///////////////////////////////////////////////////////////////////
////////////// ADP Interrupt Handlers ///////////////////////////////
///////////////////////////////////////////////////////////////////*/
/**
 * This function compares Ramp Timer values
 */
static uint32_t set_timer_value(dwc_otg_core_if_t *core_if, uint32_t val)
{
	if (core_if->adp.probe_timer_values[0] == -1) {
		core_if->adp.probe_timer_values[0] = val;
		core_if->adp.probe_timer_values[1] = -1;
		return 1;
	} else {
		core_if->adp.probe_timer_values[1] =
		    core_if->adp.probe_timer_values[0];
		core_if->adp.probe_timer_values[0] = val;
		return 0;
	}
}

/**
 * This function compares Ramp Timer values
 */
static uint32_t compare_timer_values(dwc_otg_core_if_t *core_if)
{
	if (core_if->adp.probe_timer_values[0] ==
	    core_if->adp.probe_timer_values[1]) {
		return 0;
	} else {
		return 1;
	}
}

/**
 * This function hadles ADP Probe and Timeout Interrupts
 */
static int32_t dwc_otg_adp_handle_prb_tmout_intr(dwc_otg_core_if_t *core_if,
						 uint32_t val)
{
	adpctl_data_t adpctl = {.d32 = 0 };
	gpwrdn_data_t gpwrdn;

	adpctl.d32 = val;

	if (!set_timer_value(core_if, adpctl.b.rtim) &&
	    core_if->adp.initial_probe) {
		core_if->adp.initial_probe = 0;
		dwc_otg_adp_probe_stop(core_if);

		gpwrdn.d32 = 0;
		gpwrdn.b.pmuactv = 0;
		dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);

		gpwrdn.d32 = dwc_read_reg32(&core_if->core_global_regs->gpwrdn);
		/* check which value is for device mode and which for Host mode */
		if (gpwrdn.b.idsts) {	/* considered host mode value is 1 */
			/*
			 * Turn on VBUS after initial ADP probe.
			 */
			core_if->op_state = A_HOST;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_hcd_start(core_if);
			dwc_otg_adp_turnon_vbus(core_if);
		} else {
			/*
			 * Initiate SRP after initial ADP probe.
			 */
			core_if->op_state = B_PERIPHERAL;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_pcd_start(core_if);
			dwc_otg_initiate_srp(core_if);
		}
	} else {
		gpwrdn.d32 = dwc_read_reg32(&core_if->core_global_regs->gpwrdn);

		if (compare_timer_values(core_if)) {
/*			core_if->adp.attached = DWC_OTG_ADP_ATTACHED; */
			dwc_otg_adp_probe_stop(core_if);

			/* Power on the core */
			if (core_if->power_down == 2) {
				gpwrdn.b.pwrdnswtch = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, 0, gpwrdn.d32);
			}

			/* check which value is for device mode and which for Host mode */
			if (gpwrdn.b.idsts) {	/* considered host mode value is 1 */

				/* Disable Power Down Logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.pmuactv = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);

				/*
				 * Initialize the Core for Host mode.
				 */
				core_if->op_state = A_HOST;
				dwc_otg_core_init(core_if);
				dwc_otg_enable_global_interrupts(core_if);
				cil_hcd_start(core_if);
			} else {
				/* Mask SRP detected interrupt from Power Down Logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.srp_det_msk = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);

				/* Disable Power Down Logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.pmuactv = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);

				/*
				 * Initialize the Core for Device mode.
				 */
				core_if->op_state = B_PERIPHERAL;
				dwc_otg_core_init(core_if);
				dwc_otg_enable_global_interrupts(core_if);
				cil_pcd_start(core_if);

				if (!gpwrdn.b.bsessvld)
					dwc_otg_initiate_srp(core_if);
			}
		}

		if (gpwrdn.b.bsessvld) {
			/* Mask SRP detected interrupt from Power Down Logic */
			gpwrdn.d32 = 0;
			gpwrdn.b.srp_det_msk = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn,
					 gpwrdn.d32, 0);

			/* Disable Power Down Logic */
			gpwrdn.d32 = 0;
			gpwrdn.b.pmuactv = 1;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn,
					 gpwrdn.d32, 0);

			/*
			 * Initialize the Core for Device mode.
			 */
			core_if->op_state = B_PERIPHERAL;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_pcd_start(core_if);
		}

	}
	return 0;
}

/**
 * This function hadles ADP Sense Interrupt
 */
static int32_t dwc_otg_adp_handle_sns_intr(dwc_otg_core_if_t *core_if)
{
	/* Stop ADP Sense timer */
	DWC_TIMER_CANCEL(core_if->adp.sense_timer);

	/* Restart ADP Sense timer */
	dwc_otg_adp_sense_timer_start(core_if);

	return 0;
}

/**
 * ADP Interrupt handler.
 *
 */
int32_t dwc_otg_adp_handle_intr(dwc_otg_core_if_t *core_if)
{
	int retval = 0;
	adpctl_data_t adpctl;

	adpctl.d32 = dwc_otg_adp_read_reg(core_if);

	if (adpctl.b.adp_sns_int & adpctl.b.adp_sns_int_msk)
		retval |= dwc_otg_adp_handle_sns_intr(core_if);

	if ((adpctl.b.adp_prb_int & adpctl.b.adp_prb_int_msk) ||
	    adpctl.b.adp_tmout_int & adpctl.b.adp_tmout_int_msk) {
		retval |=
		    dwc_otg_adp_handle_prb_tmout_intr(core_if, adpctl.d32);
	}

	adpctl.d32 = 0;
	adpctl.b.adp_prb_int = 1;
	adpctl.b.adp_tmout_int = 1;
	adpctl.b.adp_sns_int = 1;

	dwc_otg_adp_modify_reg(core_if, adpctl.d32, 0);

	return retval;
}

/**
 *
 * @param core_if Programming view of DWC_otg controller.
 */
int32_t dwc_otg_adp_handle_srp_intr(dwc_otg_core_if_t *core_if)
{
	hprt0_data_t hprt0;
	gpwrdn_data_t gpwrdn;

#ifndef DWC_HOST_ONLY
	DWC_DEBUGPL(DBG_ANY,
		    "++ Power Down Logic Session Request Interrupt++\n");

	gpwrdn.d32 = dwc_read_reg32(&core_if->core_global_regs->gpwrdn);
	/* check which value is for device mode and which for Host mode */
	if (gpwrdn.b.idsts) {	/* considered host mode value is 1 */
		DWC_PRINTF("SRP: Host mode\n");

		if (core_if->adp_enable) {
			dwc_otg_adp_probe_stop(core_if);

			/* Power on the core */
			if (core_if->power_down == 2) {
				gpwrdn.b.pwrdnswtch = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, 0, gpwrdn.d32);
			}

			core_if->op_state = A_HOST;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_hcd_start(core_if);
		}

		/* Turn on the port power bit. */
		hprt0.d32 = dwc_otg_read_hprt0(core_if);
		hprt0.b.prtpwr = 1;
		dwc_write_reg32(core_if->host_if->hprt0, hprt0.d32);

		/* Start the Connection timer. So a message can be displayed
		 * if connect does not occur within 10 seconds. */
		cil_hcd_session_start(core_if);
	} else {
		DWC_PRINTF("SRP: Device mode\n");
		if (core_if->adp_enable) {
			dwc_otg_adp_probe_stop(core_if);

			/* Power on the core */
			if (core_if->power_down == 2) {
				gpwrdn.b.pwrdnswtch = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gpwrdn, 0, gpwrdn.d32);
			}

			gpwrdn.d32 = 0;
			gpwrdn.b.pmuactv = 0;
			dwc_modify_reg32(&core_if->core_global_regs->gpwrdn, 0,
					 gpwrdn.d32);

			core_if->op_state = B_PERIPHERAL;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_pcd_start(core_if);
		}
	}
#endif
	return 1;
}

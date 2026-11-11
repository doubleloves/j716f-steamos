diff --git a/sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c b/sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c
--- a/sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c
+++ b/sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c
@@ -87,9 +87,42 @@
 {
 	struct q6dsp_clk *clk = to_q6dsp_clk(hw);
 	struct q6dsp_cc *cc = dev_get_drvdata(clk->dev);
-
-	return cc->desc->lpass_vote_clk(clk->dev, clk->q6dsp_clk_id,
-				  clk_hw_get_name(&clk->hw), &clk->handle);
+	int ret;
+
+	ret = cc->desc->lpass_vote_clk(clk->dev, clk->q6dsp_clk_id,
+				       clk_hw_get_name(&clk->hw), &clk->handle);
+
+	/*
+	 * Lenovo J716F: the ADSP firmware on this board answers
+	 * AFE_CMD_REMOTE_LPASS_CORE_HW_VOTE_REQUEST (0x100f4) with
+	 * ADSP_EUNSUPPORTED / -EINVAL:
+	 *
+	 *   qcom-q6afe aprsvc:service:4:4: cmd = 0x100f4 returned error = 0x16
+	 *   qcom-q6afe aprsvc:service:4:4: Unknown cmd 0x100f4
+	 *   qcom-q6afe aprsvc:service:4:4: AFE failed to vote (3)
+	 *
+	 * The vote only tells the DSP that a host client wants the LPASS core
+	 * hardware block kept powered. A rejected vote is not fatal for a
+	 * platform whose LPASS is already clocked and powered by the
+	 * bootloader, but returning the error here makes clk_prepare() fail,
+	 * which in turn blocks LPASS_HW_MACRO / LPASS_HW_DCODEC from ever
+	 * being enabled - and with those gates closed the macro register
+	 * writes land in a block that is not running, so the DMIC clock
+	 * generators never produce a clock and every capture path returns
+	 * silence.
+	 *
+	 * Log it once and carry on: the enable side (clk_vote_q6dsp_block's
+	 * counterpart) still programs the block, and unvote tolerates a NULL
+	 * handle.
+	 */
+	if (ret) {
+		dev_dbg(clk->dev,
+			"J716F: LPASS core vote for %s failed (%d), continuing\n",
+			clk_hw_get_name(&clk->hw), ret);
+		return 0;
+	}
+
+	return 0;
 }
 
 static void clk_unvote_q6dsp_block(struct clk_hw *hw)


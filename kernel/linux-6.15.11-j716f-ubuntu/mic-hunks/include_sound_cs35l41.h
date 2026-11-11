diff --git a/include/sound/cs35l41.h b/include/sound/cs35l41.h
--- a/include/sound/cs35l41.h
+++ b/include/sound/cs35l41.h
@@ -38,6 +38,9 @@
 #define CS35L41_PWR_CTRL3		0x0000201C
 #define CS35L41_CTRL_OVRRIDE		0x00002020
 #define CS35L41_AMP_OUT_MUTE		0x00002024
+#define CS35L41_AMP_OUT_MUTE_MASK	0x00000010
+/* Vendor driver clears this IRQ1_MASK1 bit when enabling the amp. */
+#define CS35L41_IRQ1_MASK1_PUP_UNMASK	0x00001000
 #define CS35L41_PROTECT_REL_ERR_IGN	0x00002034
 #define CS35L41_GPIO_PAD_CONTROL	0x0000242C
 #define CS35L41_JTAG_CONTROL		0x00002438
@@ -61,6 +64,7 @@
 #define CS35L41_MDSYNC_SYNC_PTE3	0x0000352C
 #define CS35L41_MDSYNC_SYNC_MSM_STATUS	0x0000353C
 #define CS35L41_BSTCVRT_VCTRL1		0x00003800
+#define CS35L41_BST_VCTL_MASK		0x000000ff
 #define CS35L41_BSTCVRT_VCTRL2		0x00003804
 #define CS35L41_BSTCVRT_PEAK_CUR	0x00003808
 #define CS35L41_BSTCVRT_SFT_RAMP	0x0000380C
@@ -798,6 +802,7 @@
 	int bst_ind;
 	int bst_ipk;
 	int bst_cap;
+	int bst_vctl_mv;
 	int dout_hiz;
 	struct cs35l41_gpio_cfg gpio1;
 	struct cs35l41_gpio_cfg gpio2;


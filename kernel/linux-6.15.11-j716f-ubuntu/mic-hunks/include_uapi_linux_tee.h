diff --git a/include/uapi/linux/tee.h b/include/uapi/linux/tee.h
--- a/include/uapi/linux/tee.h
+++ b/include/uapi/linux/tee.h
@@ -57,6 +57,7 @@
 #define TEE_IMPL_ID_OPTEE	1
 #define TEE_IMPL_ID_AMDTEE	2
 #define TEE_IMPL_ID_TSTEE	3
+#define TEE_IMPL_ID_QSEECOM	5
 
 /*
  * OP-TEE specific capabilities


diff --git a/sound/soc/qcom/qdsp6/q6routing.c b/sound/soc/qcom/qdsp6/q6routing.c
--- a/sound/soc/qcom/qdsp6/q6routing.c
+++ b/sound/soc/qcom/qdsp6/q6routing.c
@@ -396,6 +396,7 @@
 			      session->channels, topology, perf_mode,
 			      session->bits_per_sample, 0, 0);
 
+
 	if (IS_ERR_OR_NULL(copp)) {
 		mutex_unlock(&routing_data->lock);
 		return -EINVAL;
@@ -414,8 +415,12 @@
 	if (num_copps) {
 		payload.num_copps = num_copps;
 		payload.session_id = stream_id;
-		q6adm_matrix_map(routing_data->dev, session->path_type,
-				 payload, perf_mode);
+		{
+			int mret = q6adm_matrix_map(routing_data->dev,
+						    session->path_type,
+						    payload, perf_mode);
+
+		}
 	}
 	mutex_unlock(&routing_data->lock);
 


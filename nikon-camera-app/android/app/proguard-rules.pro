# Keep native bridge
-keep class com.nikon.app.jni.CameraBridge { *; }

# Keep data classes used by JNI
-keep class com.nikon.app.viewmodel.CameraInfo { *; }
-keep class com.nikon.app.viewmodel.CameraFile { *; }
-keep class com.nikon.app.viewmodel.TransferJob { *; }

# Keep PictureControl struct (used by JNI byte array)
-keep class com.nikon.app.jni.** { *; }

# Compose
-dontwarn androidx.compose.**

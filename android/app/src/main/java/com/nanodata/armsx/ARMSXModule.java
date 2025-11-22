package com.nanodata.armsx;

import android.app.Activity;
import android.content.Intent;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;
import com.facebook.react.bridge.ReadableArray;

import java.util.ArrayList;
import java.util.List;

public class ARMSXModule extends ReactContextBaseJavaModule {
    public static final String NAME = "ARMSXModule";

    public ARMSXModule(ReactApplicationContext reactContext) {
        super(reactContext);
    }

    @NonNull
    @Override
    public String getName() {
        return NAME;
    }

    @ReactMethod
    public void loadEmu(@Nullable ReadableArray args, Promise promise) {
        Activity host = getCurrentActivity();
        if (host == null) {
            promise.reject("no_activity", "Activity not available");
            return;
        }

        Intent intent = new Intent(host, EmulatorActivity.class);
        intent.putExtra(EmulatorActivity.EXTRA_NATIVE_ARGS, readableArrayToStrings(args));
        host.startActivity(intent);
        promise.resolve(true);
    }

    private @Nullable String[] readableArrayToStrings(@Nullable ReadableArray array) {
        if (array == null) {
            return null;
        }
        List<String> values = new ArrayList<>();
        for (int i = 0; i < array.size(); i++) {
            if (!array.isNull(i)) {
                values.add(array.getString(i));
            }
        }
        return values.isEmpty() ? null : values.toArray(new String[0]);
    }
}

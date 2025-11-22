package com.nanodata.armsx;

import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.facebook.react.ReactInstanceManager;
import com.facebook.react.ReactInstanceManagerBuilder;
import com.facebook.react.ReactPackage;
import com.facebook.react.ReactRootView;
import com.facebook.react.common.LifecycleState;
import com.facebook.react.modules.core.DefaultHardwareBackBtnHandler;
import com.facebook.react.shell.MainReactPackage;

import java.util.ArrayList;
import java.util.List;

public class OverlayActivity extends AppCompatActivity implements DefaultHardwareBackBtnHandler {
    private ReactRootView reactRootView;
    private ReactInstanceManager reactInstanceManager;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mountReactSurface();
    }

    private void mountReactSurface() {
        if (reactRootView != null && reactInstanceManager != null) {
            setContentView(reactRootView);
            return;
        }

        try {
            reactRootView = new ReactRootView(this);

            ReactInstanceManagerBuilder builder = ReactInstanceManager.builder()
                    .setApplication(getApplication())
                    .setCurrentActivity(this)
                    .setUseDeveloperSupport(BuildConfig.DEBUG)
                    .setInitialLifecycleState(LifecycleState.RESUMED);

            List<ReactPackage> packages = new ArrayList<>();
            packages.add(new MainReactPackage());
            packages.add(new ARMSXPackage());
            for (ReactPackage reactPackage : packages) {
                builder.addPackage(reactPackage);
            }

            if (BuildConfig.DEBUG) {
                builder.setJSMainModulePath("mobile/index");
            } else {
                builder.setBundleAssetName("index.android.bundle");
            }

            reactInstanceManager = builder.build();
            reactRootView.startReactApplication(reactInstanceManager, "ARMSXOverlay", null);
            setContentView(reactRootView);
        } catch (Throwable throwable) {
            TextView fallback = new TextView(this);
            fallback.setGravity(Gravity.CENTER);
            fallback.setText("React Native failed to load. Connect Metro and reload.");
            setContentView(fallback);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (reactInstanceManager != null) {
            reactInstanceManager.onHostResume(this, this);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (reactInstanceManager != null) {
            reactInstanceManager.onHostPause(this);
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (!isChangingConfigurations()) {
            teardownReact();
        }
    }

    @Override
    protected void onDestroy() {
        teardownReact();
        super.onDestroy();
    }

    private void teardownReact() {
        if (reactRootView != null) {
            reactRootView.unmountReactApplication();
            reactRootView = null;
        }
        if (reactInstanceManager != null) {
            reactInstanceManager.onHostDestroy(this);
            reactInstanceManager.destroy();
            reactInstanceManager = null;
        }
    }

    @Override
    public void invokeDefaultOnBackPressed() {
        teardownReact();
        finish();
    }

    @Override
    public void onBackPressed() {
        if (reactInstanceManager != null) {
            reactInstanceManager.onBackPressed();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (reactInstanceManager != null) {
            reactInstanceManager.onActivityResult(this, requestCode, resultCode, data);
        }
    }
}

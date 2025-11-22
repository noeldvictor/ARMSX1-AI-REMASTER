import React from 'react';
import {NativeModules, SafeAreaView, StyleSheet, Text, TouchableOpacity, View} from 'react-native';

const App = () => {
  const launchEmu = async () => {
    try {
      await NativeModules.ARMSXModule?.loadEmu?.(['--use-args']);
    } catch (err) {
      console.warn('Failed to launch emulator', err);
    }
  };

  return (
    <SafeAreaView style={styles.safeArea}>
      <View style={styles.panel}>
        <View>
          <Text style={styles.title}>ARMSX</Text>
          <Text style={styles.subtitle}>React Native overlay (0.76)</Text>
        </View>

        <View style={styles.actions}>
          <TouchableOpacity
            accessibilityLabel="Open emulator menu"
            style={styles.primaryButton}
            onPress={launchEmu}>
            <Text style={styles.primaryText}>Launch Emulator</Text>
          </TouchableOpacity>
          <TouchableOpacity
            accessibilityLabel="Save state"
            style={styles.secondaryButton}
            onPress={() => console.log('Quick save')}>
            <Text style={styles.secondaryText}>Quick Save</Text>
          </TouchableOpacity>
          <TouchableOpacity
            accessibilityLabel="Load state"
            style={styles.secondaryButton}
            onPress={() => console.log('Quick load')}>
            <Text style={styles.secondaryText}>Quick Load</Text>
          </TouchableOpacity>
        </View>

        <Text style={styles.helper}>
          Wire these actions into native hooks when the bridge is ready.
        </Text>
      </View>
    </SafeAreaView>
  );
};

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: 'transparent',
    justifyContent: 'flex-end',
    padding: 16,
  },
  panel: {
    backgroundColor: 'rgba(12, 18, 32, 0.85)',
    borderRadius: 18,
    padding: 16,
    gap: 12,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.08)',
  },
  title: {
    color: '#e8eef9',
    fontSize: 20,
    fontWeight: '700',
    letterSpacing: 0.5,
  },
  subtitle: {
    color: '#a9b4c6',
    fontSize: 13,
    marginTop: 2,
  },
  actions: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
  },
  primaryButton: {
    backgroundColor: '#1b8ef2',
    paddingHorizontal: 14,
    paddingVertical: 10,
    borderRadius: 12,
  },
  primaryText: {
    color: '#0a1020',
    fontWeight: '700',
    fontSize: 14,
  },
  secondaryButton: {
    backgroundColor: 'rgba(255,255,255,0.08)',
    paddingHorizontal: 12,
    paddingVertical: 10,
    borderRadius: 12,
  },
  secondaryText: {
    color: '#e8eef9',
    fontWeight: '600',
    fontSize: 14,
  },
  helper: {
    color: '#8da0b8',
    fontSize: 12,
  },
});

export default App;

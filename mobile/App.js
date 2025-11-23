import React, {useEffect, useMemo, useState} from 'react';
import {
  NativeModules,
  SafeAreaView,
  StatusBar,
  StyleSheet,
  Text,
  TextInput,
  TouchableOpacity,
  ScrollView,
  View,
} from 'react-native';

const {ARMSXModule} = NativeModules;

const TONALITY = {
  primary: ['#3E8CA5', '#49a7c6'],
  success: ['#4EA35B', '#5dc66d'],
  warning: ['#F19C42', '#ffb544'],
  error: ['#B62F28', '#E93F36'],
};

const App = () => {
  const [biosPath, setBiosPath] = useState('');
  const [cdromPath, setCdromPath] = useState('');
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);
  const [libraryPath, setLibraryPath] = useState('');
  const [games, setGames] = useState([]);

  const launchEmu = async () => {
    const trimmedBios = biosPath.trim();
    const trimmedCd = cdromPath.trim();

    const args = ['--use-args'];
    if (trimmedBios) {
      args.push('--bios', trimmedBios);
    }
    if (trimmedCd) {
      args.push('--cdrom', trimmedCd);
    }

    setBusy(true);
    setStatus(
      `Booting${trimmedBios ? ` BIOS ${trimmedBios}` : ''}${
        trimmedCd ? ` | CD ${trimmedCd}` : ''
      }`,
    );

    try {
      await ARMSXModule?.forceLandscape?.();
      await ARMSXModule?.loadEmu?.(args);
    } catch (err) {
      console.warn('Failed to launch emulator', err);
      setStatus('Launch failed. Check paths and try again.');
    } finally {
      setBusy(false);
    }
  };

  const tonality = useMemo(() => TONALITY.primary, []);

  const Slot = ({label, value, placeholder, onChange, actionLabel, onAction}) => (
    <View style={styles.slot}>
      <Text style={styles.slotLabel}>{label}</Text>
      <View style={styles.inputRow}>
        <TextInput
          placeholder={placeholder}
          placeholderTextColor="#C6C843"
          value={value}
          onChangeText={onChange}
          style={styles.input}
          autoCapitalize="none"
          autoCorrect={false}
        />
        <PSXButton
          label={actionLabel}
          tone="primary"
          onPress={onAction}
          compact
        />
      </View>
    </View>
  );

  const PSXButton = ({label, tone = 'primary', onPress, compact, disabled}) => (
    <TouchableOpacity
      activeOpacity={0.9}
      disabled={disabled}
      onPress={onPress}
      style={[
        styles.button,
        styles[`button_${tone}`],
        compact && styles.buttonCompact,
        disabled && styles.buttonDisabled,
      ]}>
      <Text
        style={[
          styles.buttonText,
          styles[`buttonText_${tone}`],
          compact && styles.buttonTextCompact,
          disabled && styles.buttonTextDisabled,
        ]}>
        {label}
      </Text>
    </TouchableOpacity>
  );

  const tryPick = async kind => {
    try {
      const picker = ARMSXModule?.pickPath || ARMSXModule?.pickFile;
      if (!picker) {
        setStatus('No native picker available, paste a path manually.');
        return;
      }
      const value = await picker(kind);
      if (kind === 'bios') {
        setBiosPath(value ?? '');
      } else {
        setCdromPath(value ?? '');
      }
    } catch (err) {
      console.warn('Picker failed', err);
      setStatus('Picker unavailable, enter the full path manually.');
    }
  };

  const refreshLibrary = async () => {
    if (!ARMSXModule?.ensureGameFolder || !ARMSXModule?.listGames) {
      setStatus('Native library module missing; cannot show Files folder.');
      return;
    }
    try {
      const folder = await ARMSXModule?.ensureGameFolder?.();
      if (folder) {
        setLibraryPath(folder);
      }
      const found = (await ARMSXModule?.listGames?.()) || [];
      setGames(found);
    } catch (err) {
      console.warn('Library scan failed', err);
      setStatus('Unable to read local library. Check Files access.');
    }
  };

  useEffect(() => {
    refreshLibrary();
  }, []);

  return (
    <SafeAreaView style={styles.safeArea}>
      <StatusBar barStyle="light-content" />
      <View style={styles.backdrop} />
      <View style={styles.container}>
        <Text style={styles.logo}>ARMSX</Text>
        <Text style={styles.subtitle}>PS1 launcher overlay</Text>

        <View style={[styles.card, styles.tallCard]}>
          <Text style={styles.cardTitle}>Boot configuration</Text>
          <Slot
            label="BIOS"
            placeholder="SCPH-1001.bin / path"
            value={biosPath}
            onChange={setBiosPath}
            actionLabel="Locate"
            onAction={() => tryPick('bios')}
          />
          <Slot
            label="CD-ROM"
            placeholder="Game .bin/.cue/.iso path"
            value={cdromPath}
            onChange={setCdromPath}
            actionLabel="Browse"
            onAction={() => tryPick('cdrom')}
          />

          <View style={styles.actions}>
            <PSXButton
              label={busy ? 'Booting…' : 'Start Console'}
              tone="primary"
              onPress={launchEmu}
              disabled={busy}
            />
            <PSXButton
              label="Quick Save"
              tone="success"
              onPress={() => ARMSXModule?.quickSave?.()}
              disabled={busy}
            />
            <PSXButton
              label="Quick Load"
              tone="warning"
            onPress={() => ARMSXModule?.quickLoad?.()}
            disabled={busy}
          />
        </View>

        <View style={[styles.card, styles.tallCard]}>
          <View style={styles.libraryHeader}>
            <Text style={styles.cardTitle}>Local library</Text>
            <PSXButton
              label="Rescan"
              tone="primary"
              compact
              onPress={refreshLibrary}
            />
          </View>
          <Text style={styles.libraryPath}>
            Files app folder: {libraryPath || 'creating...'}
          </Text>
          {games.length === 0 ? (
            <Text style={styles.emptyState}>No games found!</Text>
          ) : (
            <ScrollView style={styles.gameList} contentContainerStyle={styles.gameListContent}>
              {games.map(game => {
                const isSelected = cdromPath === game.path;
                return (
                  <TouchableOpacity
                    key={game.path}
                    style={[
                      styles.gameRow,
                      isSelected && styles.gameRowSelected,
                    ]}
                    onPress={() => {
                      setCdromPath(game.path);
                      setStatus(`Selected ${game.name}`);
                    }}>
                    <Text style={styles.gameName}>{game.name}</Text>
                    <Text style={styles.gameMeta}>
                      {isSelected ? 'Selected' : 'Tap to boot'}
                    </Text>
                  </TouchableOpacity>
                );
              })}
            </ScrollView>
          )}
        </View>
        </View>

        <View style={styles.footer}>
          <View style={[styles.progress, {borderColor: tonality[0]}]}>
            <View style={[styles.progressBar, {backgroundColor: tonality[1]}]} />
            <Text style={styles.progressLabel}>
              {status || 'Provide a BIOS and CD-ROM then press Start'}
            </Text>
          </View>
          <Text style={styles.helper}>Flags: --bios, --cdrom, --use-args</Text>
        </View>
      </View>
    </SafeAreaView>
  );
};

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: '#000',
  },
  backdrop: {
    ...StyleSheet.absoluteFillObject,
    backgroundColor: '#000',
  },
  container: {
    flex: 1,
    paddingHorizontal: 20,
    paddingVertical: 28,
    justifyContent: 'flex-end',
    gap: 18,
  },
  logo: {
    fontFamily: 'Final Fantasy Script Collection - Final Fantasy VII',
    fontSize: 46,
    color: '#d6d7dd',
    textShadowColor: '#000',
    textShadowOffset: {width: 3, height: 3},
    textShadowRadius: 2,
    lineHeight: 50,
  },
  subtitle: {
    fontFamily: 'Play',
    fontSize: 14,
    color: '#C6C843',
    letterSpacing: 2,
    marginTop: -6,
  },
  card: {
    backgroundColor: '#0D2289',
    borderWidth: 1,
    borderColor: '#c6c6c6',
    borderRadius: 10,
    padding: 16,
    shadowColor: '#000',
    shadowOpacity: 0.4,
    shadowRadius: 8,
    shadowOffset: {width: 0, height: 4},
  },
  tallCard: {
    minHeight: 200,
  },
  cardTitle: {
    fontFamily: 'Play',
    fontSize: 18,
    color: '#AAA9AF',
    letterSpacing: 3,
    textTransform: 'uppercase',
    marginBottom: 10,
    textShadowColor: '#000',
    textShadowOffset: {width: 2, height: 2},
    textShadowRadius: 1,
  },
  slot: {
    marginBottom: 14,
  },
  slotLabel: {
    fontFamily: 'Pixel Cyr Normal',
    fontSize: 14,
    color: '#d6d7dd',
    marginBottom: 6,
    letterSpacing: 1,
  },
  inputRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  input: {
    flex: 1,
    borderBottomWidth: 2,
    borderBottomColor: '#fff',
    paddingVertical: 6,
    fontFamily: 'Final Fantasy Script Collection - Final Fantasy VII',
    fontSize: 20,
    color: '#fff',
  },
  button: {
    backgroundColor: '#3E8CA5',
    paddingHorizontal: 18,
    paddingVertical: 12,
    borderRadius: 10,
    borderWidth: 2,
    borderTopColor: 'rgba(255,255,255,0.45)',
    borderBottomColor: 'rgba(255,255,255,0.15)',
    borderLeftColor: 'transparent',
    borderRightColor: 'transparent',
  },
  buttonCompact: {
    paddingHorizontal: 12,
    paddingVertical: 10,
  },
  button_primary: {
    backgroundColor: '#3E8CA5',
  },
  button_success: {
    backgroundColor: '#4EA35B',
  },
  button_warning: {
    backgroundColor: '#F19C42',
  },
  button_error: {
    backgroundColor: '#B62F28',
  },
  buttonDisabled: {
    backgroundColor: '#3a3a3a',
  },
  buttonText: {
    fontFamily: 'RationalTWDisplay',
    color: '#fff',
    fontSize: 16,
    letterSpacing: 1,
    textTransform: 'uppercase',
    textShadowColor: '#000',
    textShadowOffset: {width: 2, height: 2},
    textShadowRadius: 1,
  },
  buttonTextCompact: {
    fontSize: 14,
  },
  buttonText_primary: {
    color: '#fff',
  },
  buttonText_success: {
    color: '#f3ffe1',
  },
  buttonText_warning: {
    color: '#fff7dd',
  },
  buttonText_error: {
    color: '#ffe3e3',
  },
  buttonTextDisabled: {
    color: '#AEAFAE',
  },
  actions: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginTop: 8,
  },
  libraryHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: 8,
  },
  libraryPath: {
    fontFamily: 'Pixel Cyr Normal',
    color: '#C6C843',
    fontSize: 12,
    marginBottom: 10,
  },
  gameList: {
    maxHeight: 260,
  },
  gameListContent: {
    gap: 8,
    paddingBottom: 8,
  },
  gameRow: {
    padding: 12,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.08)',
    backgroundColor: 'rgba(0,0,0,0.25)',
  },
  gameRowSelected: {
    borderColor: '#3E8CA5',
    backgroundColor: 'rgba(62,140,165,0.25)',
  },
  gameName: {
    fontFamily: 'RationalTWDisplay',
    color: '#fff',
    fontSize: 14,
    letterSpacing: 1,
  },
  gameMeta: {
    fontFamily: 'Pixel Cyr Normal',
    color: '#AAA9AF',
    fontSize: 12,
    marginTop: 4,
  },
  emptyState: {
    fontFamily: 'Pixel Cyr Normal',
    color: '#d6d7dd',
    fontSize: 13,
  },
  footer: {
    gap: 8,
  },
  progress: {
    height: 26,
    borderWidth: 1,
    borderRadius: 8,
    overflow: 'hidden',
    backgroundColor: 'rgba(0,0,0,0.5)',
    justifyContent: 'center',
  },
  progressBar: {
    position: 'absolute',
    left: 0,
    top: 0,
    bottom: 0,
    width: '45%',
    opacity: 0.65,
  },
  progressLabel: {
    fontFamily: 'Pixel Cyr Normal',
    color: '#fff',
    textAlign: 'center',
    fontSize: 12,
    letterSpacing: 0.5,
  },
  helper: {
    fontFamily: 'Play',
    color: '#8da0b8',
    fontSize: 12,
    letterSpacing: 1,
  },
});

export default App;

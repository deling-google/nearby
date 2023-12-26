package com.google.hellocloud;

import android.graphics.drawable.Drawable;
import android.net.Uri;
import androidx.databinding.Bindable;
import com.google.android.gms.tasks.Task;
import com.google.android.gms.tasks.Tasks;
import com.google.gson.Gson;

public final class IncomingFile extends File {
  enum State {
    RECEIVED,
    DOWNLOADING,
    DOWNLOADED
  }

  public String remotePath;

  // Do not serialize
  private transient State state;
  private transient Uri localUri;

  public State getState() {
    return state;
  }

  public IncomingFile setState(State value) {
    state = value;
    notifyPropertyChanged(BR.stateIcon);
    notifyPropertyChanged(BR.isBusy);
    //    notifyPropertyChanged(BR.canDownload);
    return this;
  }

  public IncomingFile setFileSize(long fileSize) {
    this.fileSize = fileSize;
    return this;
  }

  public IncomingFile setLocalUri(Uri uri) {
    this.localUri = uri;
    return this;
  }

  @Bindable
  public boolean getIsBusy() {
    return state == State.DOWNLOADING;
  }

  @Bindable
  public Drawable getStateIcon() {
    if (state == null) {
      return null;
    }

    int resource;
    switch (state) {
      case RECEIVED -> resource = R.drawable.received;
      case DOWNLOADED -> resource = R.drawable.downloaded;
      default -> {
        return null;
      }
    }
    return Main.shared.context.getResources().getDrawable(resource, null);
  }

  public IncomingFile(String mimeType) {
    this.mimeType = mimeType;
  }

  public Task<Void> download() {
    if (state != State.RECEIVED) {
      return Tasks.forResult(null);
    }

    setState(State.DOWNLOADING);

    return CloudStorage.shared
        .download(remotePath, localUri)
        .addOnSuccessListener(result -> setState(State.DOWNLOADED))
        .addOnFailureListener(error -> setState(State.RECEIVED));
  }

  public static IncomingFile[] decodeIncomingFiles(String json) {
    Gson gson = new Gson();
    return gson.fromJson(json, IncomingFile[].class);
  }
}

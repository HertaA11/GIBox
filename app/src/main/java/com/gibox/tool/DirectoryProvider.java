package com.gibox.tool;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import java.io.File;
import java.io.FileNotFoundException;
import java.net.URLConnection;
import java.util.ArrayList;
import java.util.Collections;

/** SAF bridge: managers can browse Mods in private storage as well as the game's shared files. Only the requested
 * document/tree is granted by the app; general discovery remains protected by MANAGE_DOCUMENTS. */
public final class DirectoryProvider extends DocumentsProvider {
    public static final String AUTHORITY = "com.gibox.tool.documents";
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_FLAGS, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED};
    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE, Root.COLUMN_ICON,
        Root.COLUMN_FLAGS, Root.COLUMN_MIME_TYPES};
    @Override public boolean onCreate() { return true; }
    private File file(String id) throws FileNotFoundException {
        int colon=id.indexOf(':'), slash=id.indexOf('/');
        if(colon<0||(slash>=0&&slash<=colon)) throw new FileNotFoundException("Invalid document ID");
        String pkg=id.substring(0,colon), kind=id.substring(colon+1,slash<0?id.length():slash);
        if(!Control.supported(pkg)) throw new FileNotFoundException("Unknown game");
        File root;
        if(kind.equals("mods")) root=Control.mods(getContext().getFilesDir(),pkg);
        else if(kind.equals("shared")) root=Control.shared(pkg);
        else throw new FileNotFoundException("Unknown document root");
        return DocumentPaths.resolve(root,slash<0?"":id.substring(slash+1));
    }
    private static String type(File file) {
        if(file.isDirectory()) return Document.MIME_TYPE_DIR;
        String mime=URLConnection.guessContentTypeFromName(file.getName());
        return mime==null?"application/octet-stream":mime;
    }
    private void row(MatrixCursor cursor,String id) throws FileNotFoundException {
        File f=file(id);if(!f.exists()) throw new FileNotFoundException(id);
        int flags=0;
        // Root folders cannot be renamed/deleted; descendants are normal file-manager documents.
        if(f.canWrite()) flags=f.isDirectory()?Document.FLAG_DIR_SUPPORTS_CREATE:Document.FLAG_SUPPORTS_WRITE;
        if(id.indexOf('/')>=0&&f.getParentFile().canWrite()) flags|=Document.FLAG_SUPPORTS_DELETE|Document.FLAG_SUPPORTS_RENAME;
        MatrixCursor.RowBuilder row=cursor.newRow();
        for(String column:cursor.getColumnNames()) {
            Object value=null;
            if(column.equals(Document.COLUMN_DOCUMENT_ID)) value=id;
            else if(column.equals(Document.COLUMN_DISPLAY_NAME)) value=f.getName();
            else if(column.equals(Document.COLUMN_MIME_TYPE)) value=type(f);
            else if(column.equals(Document.COLUMN_FLAGS)) value=flags;
            else if(column.equals(Document.COLUMN_SIZE)) value=f.isFile()?f.length():null;
            else if(column.equals(Document.COLUMN_LAST_MODIFIED)) value=f.lastModified();
            row.add(value);
        }
    }
    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor result=new MatrixCursor(projection==null?ROOT_COLUMNS:projection);
        for(String pkg:new String[]{"com.miHoYo.Yuanshen","com.miHoYo.ys.bilibili","com.miHoYo.GenshinImpact"}) for(String kind:new String[]{"mods","shared"}) {
            String id=pkg+":"+kind;try{if(!file(id).isDirectory())continue;}catch(FileNotFoundException e){continue;}MatrixCursor.RowBuilder row=result.newRow();
            for(String column:result.getColumnNames()) {
                Object value=null;
                if(column.equals(Root.COLUMN_ROOT_ID)||column.equals(Root.COLUMN_DOCUMENT_ID)) value=id;
                else if(column.equals(Root.COLUMN_TITLE)) value="GIBox "+(pkg.endsWith("bilibili")?"B 服":pkg.endsWith("GenshinImpact")?"国际服":"官服")+(kind.equals("mods")?" Mods":" 文件");
                else if(column.equals(Root.COLUMN_ICON)) value=android.R.drawable.ic_menu_agenda;
                else if(column.equals(Root.COLUMN_FLAGS)) value=Root.FLAG_SUPPORTS_CREATE|Root.FLAG_SUPPORTS_IS_CHILD|Root.FLAG_LOCAL_ONLY;
                else if(column.equals(Root.COLUMN_MIME_TYPES)) value="*/*";
                row.add(value);
            }
        }
        return result;
    }
    @Override public Cursor queryDocument(String id,String[] projection) throws FileNotFoundException {
        MatrixCursor result=new MatrixCursor(projection==null?DOCUMENT_COLUMNS:projection);row(result,id);return result;
    }
    @Override public Cursor queryChildDocuments(String parent,String[] projection,String sortOrder) throws FileNotFoundException {
        MatrixCursor result=new MatrixCursor(projection==null?DOCUMENT_COLUMNS:projection);File[] files=file(parent).listFiles();
        if(files!=null) for(File f:files) {try{row(result,parent+"/"+f.getName());}catch(FileNotFoundException ignored){}}
        return result;
    }
    @Override public String getDocumentType(String id) throws FileNotFoundException {return type(file(id));}
    @Override public boolean isChildDocument(String parent,String child) {
        try{File p=file(parent),c=file(child);return !p.equals(c)&&c.getPath().startsWith(p.getPath()+File.separator);}catch(FileNotFoundException e){return false;}
    }
    @Override public DocumentsContract.Path findDocumentPath(String parent,String child) throws FileNotFoundException {
        file(child);String root=child.split("/",2)[0],start=parent==null?root:parent;
        if(!child.equals(start)&&!isChildDocument(start,child))throw new FileNotFoundException("Document outside parent");
        ArrayList<String> path=new ArrayList<>();String id=child;
        while(true){path.add(id);if(id.equals(start))break;int slash=id.lastIndexOf('/');if(slash<0)throw new FileNotFoundException("Invalid parent");id=id.substring(0,slash);}
        Collections.reverse(path);return new DocumentsContract.Path(parent==null?root:null,path);
    }
    @Override public ParcelFileDescriptor openDocument(String id,String mode,CancellationSignal signal) throws FileNotFoundException {
        File f=file(id);if(!f.isFile()) throw new FileNotFoundException(id);
        return ParcelFileDescriptor.open(f,ParcelFileDescriptor.parseMode(mode));
    }
    @Override public String createDocument(String parent,String mime,String name) throws FileNotFoundException {
        File dir=file(parent);if(!dir.isDirectory()) throw new FileNotFoundException(parent);
        String id=parent+"/"+DocumentPaths.name(name);File target=file(id);
        try{if(target.exists()||!(mime.equals(Document.MIME_TYPE_DIR)?target.mkdir():target.createNewFile())) throw new FileNotFoundException("File already exists or cannot be created");}
        catch(java.io.IOException e){throw new FileNotFoundException(e.getMessage());}return id;
    }
    @Override public void deleteDocument(String id) throws FileNotFoundException {
        if(id.indexOf('/')<0) throw new FileNotFoundException("Cannot delete root");
        File f=file(id);try{DocumentPaths.deleteTree(f);}catch(java.io.IOException e){throw new FileNotFoundException(e.getMessage());}
    }
    @Override public String renameDocument(String id,String name) throws FileNotFoundException {
        int slash=id.lastIndexOf('/');if(slash<0) throw new FileNotFoundException("Cannot rename root");
        String renamed=id.substring(0,slash+1)+DocumentPaths.name(name);File target=file(renamed);
        if(target.exists()||!file(id).renameTo(target)) throw new FileNotFoundException("Cannot rename document");return renamed;
    }
}

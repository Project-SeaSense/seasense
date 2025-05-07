// scripts/ingest-csvs.js
const { createClient } = require('@supabase/supabase-js');
const csvParse = require('csv-parse/sync');
const fs = require('fs');
const path = require('path');

const SUPABASE_URL = process.env.SUPABASE_URL;
const SUPABASE_SERVICE_ROLE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY;
const BUCKET = 'seasense-raw-file';

const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY);

async function listCsvFiles() {
  const { data, error } = await supabase.storage.from(BUCKET).list('', { limit: 1000 });
  if (error) throw error;
  // Only return CSV files that don't have a corresponding log file
  const files = data.filter(f => f.name.endsWith('.csv') && !f.name.endsWith('.log.csv'));
  const unprocessedFiles = [];
  for (const file of files) {
    if (!await fileExists(file.name)) {
      unprocessedFiles.push(file);
    }
  }
  return unprocessedFiles;
}

async function downloadFile(filename) {
  const { data, error } = await supabase.storage.from(BUCKET).download(filename);
  if (error) throw error;
  return Buffer.from(await data.arrayBuffer()).toString('utf-8');
}

async function uploadLogFile(filename, content) {
  const logName = filename.replace(/\.csv$/, '.log');
  await supabase.storage.from(BUCKET).upload(logName, content, { upsert: true, contentType: 'text/plain' });
}

async function fileExists(filename) {
  const logName = filename.replace(/\.csv$/, '.log');
  const { data, error } = await supabase.storage.from(BUCKET).list('');
  if (error) throw error;
  return data.some(f => f.name === logName);
}

async function insertRecord(row, columns) {
  // Map CSV row to object
  const obj = {};
  columns.forEach((col, i) => {
    obj[col] = row[i] === "" ? null : row[i];
  });
  // Insert into seasense_raw
  const { error } = await supabase.from('seasense_raw').insert([obj]);
  return error;
}

async function insertRecords(rows, columns) {
  // Map CSV rows to objects
  const objects = rows.map(row => {
    const obj = {};
    columns.forEach((col, i) => {
      obj[col] = row[i] === "" ? null : row[i];
    });
    return obj;
  });
  
  // Insert batch into seasense_raw
  const { error } = await supabase.from('seasense_raw').insert(objects);
  return error;
}

async function processFile(filename) {
  if (await fileExists(filename)) {
    return { processed: false };
  }
  console.log(`Processing ${filename}...`);
  const csvText = await downloadFile(filename);
  const records = csvParse.parse(csvText, { skip_empty_lines: true });
  const columns = records[0];
  const dataRows = records.slice(1);

  let ingested = 0;
  let failed = [];
  
  // Process in batches of 1000
  const BATCH_SIZE = 1000;
  for (let i = 0; i < dataRows.length; i += BATCH_SIZE) {
    const batch = dataRows.slice(i, i + BATCH_SIZE);
    const error = await insertRecords(batch, columns);
    if (error) {
      // If batch insert fails, try individual inserts to identify problematic rows
      for (const row of batch) {
        const singleError = await insertRecord(row, columns);
        if (singleError) {
          failed.push([...row, singleError.message]);
        } else {
          ingested++;
        }
      }
    } else {
      ingested += batch.length;
    }
  }

  // Prepare log
  let log = `${dataRows.length} records counted\n${ingested} records ingested\n`;
  if (failed.length > 0) {
    log += 'failed records:\n';
    log += columns.join(',') + ',reason\n';
    for (const row of failed) {
      log += row.join(',') + '\n';
    }
  }
  await uploadLogFile(filename, log);
  console.log(`Finished ${filename}: ${ingested} ingested, ${failed.length} failed.`);
  
  return {
    processed: true,
    filename,
    total: dataRows.length,
    ingested,
    failed: failed.length
  };
}

async function main() {
  const files = await listCsvFiles();
  const results = [];
  
  for (const file of files) {
    const result = await processFile(file.name);
    if (result.processed) {
      results.push(result);
    }
  }
  
  // Output results in a format GitHub Actions can use
  if (results.length > 0) {
    console.log('::group::Processing Results');
    console.log('Files processed:');
    results.forEach(r => {
      console.log(`- ${r.filename}: ${r.ingested} ingested, ${r.failed} failed`);
    });
    console.log('::endgroup::');
    
    // Set output for GitHub Actions
    const totalIngested = results.reduce((sum, r) => sum + r.ingested, 0);
    const totalFailed = results.reduce((sum, r) => sum + r.failed, 0);
    console.log(`::set-output name=has_activity::true`);
    console.log(`::set-output name=total_ingested::${totalIngested}`);
    console.log(`::set-output name=total_failed::${totalFailed}`);
    console.log(`::set-output name=files_processed::${results.length}`);
  } else {
    console.log('::set-output name=has_activity::false');
  }
}

main().catch(e => {
  console.error(e);
  process.exit(1);
});

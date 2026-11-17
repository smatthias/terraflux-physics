export function validateCustomer(input){
  if(!input||typeof input.name!=='string'||!input.name.trim())throw Error('Customer needs a name');
  const result={};for(const [key,max] of [['name',120],['email',254],['phone',50],['address',500],['notes',1000]]){
    const value=input[key]??'';if(typeof value!=='string'||value.length>max)throw Error('Invalid customer '+key);result[key]=value.trim();
  }
  if(result.email&&!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(result.email))throw Error('Invalid customer email');
  return result;
}
export function filterCustomers(customers,query=''){
  const words=query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return customers.filter(c=>words.every(w=>[c.name,c.email,c.phone,c.address].join(' ').toLowerCase().includes(w)));
}
export function exportCustomerBackup(customer){
  return JSON.stringify({format:'terratec-customer-backup',version:1,customer:validateCustomer(customer)},null,2)+'\n';
}
export function parseCustomerBackup(input){
  if(input?.format!=='terratec-customer-backup'||input.version!==1)throw Error('Unsupported customer backup format');
  return validateCustomer(input.customer);
}
